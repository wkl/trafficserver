/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

/****************************************************************************

  ClusterLib.cc
****************************************************************************/

#include "P_Cluster.h"
//
// cluster_xxx() functions dealing with scheduling of Virtual Connections
// in the read and write data buckets (read_vcs, write_vcs).
//
// In contrast to the net versions, these versions simply change the priority
// scheduling only occurs after they move into the data_bucket.
//
void
cluster_set_priority(ClusterHandler * nh, ClusterVConnState * ns, int new_priority)
{
  //
  // Note: Only sets priority.  No scheduling action.
  //
  if (new_priority >= CLUSTER_BUCKETS)
    ns->priority = CLUSTER_BUCKETS - 1;
  else
    ns->priority = new_priority;

  if (ns->priority < nh->min_priority)
    ns->priority = nh->min_priority;
}

void
cluster_lower_priority(ClusterHandler * ch, ClusterVConnState * ns)
{
  //
  // Note: Only computes priority.  No scheduling action.
  //
  int offset = ns->priority / 8;
  if (!offset)
    offset = 1;
  cluster_set_priority(ch, ns, ns->priority + offset);
}

void
cluster_raise_priority(ClusterHandler * ch, ClusterVConnState * ns)
{
  //
  // Note: Only computes priority.  No scheduling action.
  //
  int offset = ns->priority / 2;
  if (!offset)
    offset = 1;
  cluster_set_priority(ch, ns, ns->priority - offset);
}

void
cluster_schedule(ClusterHandler * ch, ClusterVConnection * vc, ClusterVConnState * ns)
{
  //
  // actually schedule into new bucket
  //
  int new_bucket = ch->cur_vcs + ns->priority;
  new_bucket %= CLUSTER_BUCKETS;
  if (ns == &vc->read) {
    ClusterVC_enqueue_read(ch->read_vcs[new_bucket], vc);
  } else {
    ClusterVC_enqueue_write(ch->write_vcs[new_bucket], vc);
  }
}

void
cluster_reschedule(ClusterHandler * ch, ClusterVConnection * vc, ClusterVConnState * ns)
{
  //
  // Remove from bucket, computer priority and schedule into target bucket
  //
  if (ns == &vc->read)
    ClusterVC_remove_read(vc);
  else
    ClusterVC_remove_write(vc);
  cluster_set_priority(ch, ns, ns->priority);
  cluster_schedule(ch, vc, ns);
}

void
cluster_disable(ClusterHandler * ch, ClusterVConnection * vc, ClusterVConnState * ns)
{
  //
  // disable inactivity timeout, recompute priority, remove from current
  //   bucket and reschedule into new bucket based on new priority.
  //
  ns->enabled = 0;
  if (vc->inactivity_timeout) {
    bool disable_timeout = false;
    if (ns == &vc->read) {
      disable_timeout = !vc->write.enabled;
    } else {
      disable_timeout = !vc->read.enabled;
    }
    if (disable_timeout) {
      vc->inactivity_timeout->cancel();
      vc->inactivity_timeout = NULL;
    }
  }
  cluster_lower_priority(ch, ns);
  if (ns == &vc->read)
    ClusterVC_remove_read(vc);
  else
    ClusterVC_remove_write(vc);
  cluster_schedule(ch, vc, ns);
}

void
cluster_update_priority(ClusterHandler * ch, ClusterVConnection * vc, ClusterVConnState * ns, int64_t ndone, int64_t ntodo)
{
  //
  // Enable inactivity timeout, recompute priority based on latest transfer.
  //
  if (!ns->enabled) {
    cluster_lower_priority(ch, ns);
    return;
  }
  if (vc->inactivity_timeout) {
    vc->inactivity_timeout->schedule_every(vc->inactivity_timeout->period);
  } else {
    if (vc->inactivity_timeout_in)
      vc->inactivity_timeout = vc->thread->schedule_in(vc, vc->inactivity_timeout_in);
  }

  int64_t tsize = ns->vio.buffer.total_size();

  if (tsize > ntodo) {
    tsize = ntodo;
  }
  if (ndone > tsize / 2) {
    cluster_raise_priority(ch, ns);
  } else {
    if (ndone < tsize / 4)
      cluster_lower_priority(ch, ns);
  }
  //
  // Otherwise, do not change priority
  //
}

void
cluster_bump(ClusterHandler * ch, ClusterVConnectionBase * vc, ClusterVConnState * ns, int this_bucket)
{
  //
  // Remove from current bucket and place in
  // current bucket + CLUSTER_BUMP_LENGTH
  //
  int new_bucket = (ch->cur_vcs + CLUSTER_BUMP_LENGTH) % CLUSTER_BUCKETS;

  if (ns == &vc->read) {
    if (this_bucket != CLUSTER_BUMP_NO_REMOVE) {
      ClusterVC_remove_read(vc);
    }
    ClusterVC_enqueue_read(ch->read_vcs[new_bucket], vc);
  } else {
    if (this_bucket != CLUSTER_BUMP_NO_REMOVE) {
      ClusterVC_remove_write(vc);
    }
    ClusterVC_enqueue_write(ch->write_vcs[new_bucket], vc);
  }
}

//
//  iov_memcpy() -- struct iov based memcpy()
//
int
iov_memcpy(IOVec * iov, int n_iov, int did, char *bb_arg)
{
  char *bb = bb_arg;
  for (int i = 0; i < n_iov; i++) {
    int len = iov[i].iov_len;
    did -= len;
    if (did < 0) {
      // fprintf(stderr,"iov_memcpy %X %d\n",iov[i].iov_base+(len+did),-did);
      memcpy(bb, (char *) iov[i].iov_base + (len + did), -did);
      bb += -did;
      did = 0;
    }
  }
  return bb - bb_arg;
}

/*************************************************************************/
// ClusterVCToken member functions (Public Class)
/*************************************************************************/

// global sequence number for building tokens
unsigned int cluster_sequence_number = 0;

void
ClusterVCToken::alloc()
{
#ifdef LOCAL_CLUSTER_TEST_MODE
  ip_created = this_cluster_machine()->cluster_port;
#else
  ip_created = this_cluster_machine()->ip;
#endif
  sequence_number = ink_atomic_increment((int *) &cluster_sequence_number, 1);
}

///////////////////////////////////////////
// IOBufferBlock manipulation routines
///////////////////////////////////////////

IOBufferBlock *
clone_IOBufferBlockList(IOBufferBlock * b, int start_off, int n, IOBufferBlock ** b_tail)
{
  ////////////////////////////////////////////////////////////////
  // Create a clone list of IOBufferBlock(s) where the sum
  // of all block read_avail is 'n'.  The given source list
  // must contain at least 'n' read avail bytes.
  ////////////////////////////////////////////////////////////////
  int64_t nbytes = n;
  int64_t block_read_avail;
  int64_t bytes_to_skip = start_off;
  IOBufferBlock *bsrc = b;
  IOBufferBlock *bclone = 0;
  IOBufferBlock *bclone_head = 0;

  while (bsrc && nbytes) {
    // Skip zero length blocks
    if (!bsrc->read_avail()) {
      bsrc = bsrc->next;
      continue;
    }

    if (bclone_head) {
      bclone->next = bsrc->clone();
      bclone = bclone->next;
    } else {

      // Skip bytes already processed
      if (bytes_to_skip) {
        bytes_to_skip -= bsrc->read_avail();

        if (bytes_to_skip < 0) {
          // Skip bytes in current block
          bclone_head = bsrc->clone();
          bclone_head->consume(bsrc->read_avail() + bytes_to_skip);
          bclone = bclone_head;
          bytes_to_skip = 0;

        } else {
          // Skip entire block
          bsrc = bsrc->next;
          continue;
        }
      } else {
        bclone_head = bsrc->clone();
        bclone = bclone_head;
      }
    }
    block_read_avail = bclone->read_avail();
    nbytes -= block_read_avail;
    if (nbytes < 0) {
      // Adjust read_avail in clone to match nbytes
      bclone->fill(nbytes);
      nbytes = 0;
    }
    bsrc = bsrc->next;
  }
  ink_release_assert(!nbytes);
  *b_tail = bclone;
  return bclone_head;
}

IOBufferBlock *
consume_IOBufferBlockList(IOBufferBlock * b, int64_t n)
{
  IOBufferBlock *b_remainder = 0;
  int64_t nbytes = n;

  while (b) {
    nbytes -= b->read_avail();
    if (nbytes <= 0) {
      if (nbytes < 0) {
        // Consumed a partial block, clone remainder
        b_remainder = b->clone();
        b->fill(nbytes);        // make read_avail match nbytes
        b_remainder->consume(b->read_avail());  // clone for remaining bytes
        b_remainder->next = b->next;
        b->next = 0;
        nbytes = 0;

      } else {
        // Consumed entire block
        b_remainder = b->next;
      }
      break;

    } else {
      b = b->next;
    }
  }
  ink_release_assert(nbytes == 0);
  return b_remainder;           // return remaining blocks
}

int64_t
bytes_IOBufferBlockList(IOBufferBlock * b, int64_t read_avail_bytes)
{
  int64_t n = 0;;

  while (b) {
    if (read_avail_bytes) {
      n += b->read_avail();
    } else {
      n += b->write_avail();
    }
    b = b->next;
  }
  return n;
}


//////////////////////////////////////////////////////
// Miscellaneous test code
//////////////////////////////////////////////////////
#if TEST_PARTIAL_READS
//
// Test code which mimic the network slowdown
//
int
partial_readv(int fd, IOVec * iov, int n_iov, int seq)
{
  IOVec tiov[16];
  for (int i = 0; i < n_iov; i++)
    tiov[i] = iov[i];
  int tn_iov = n_iov;
  int rnd = seq;
  int element = rand_r((unsigned int *) &rnd);
  element = element % n_iov;
  int byte = rand_r((unsigned int *) &rnd);
  byte = byte % iov[element].iov_len;
  int stop = rand_r((unsigned int *) &rnd);
  if (!(stop % 3)) {            // 33% chance
    tn_iov = element + 1;
    tiov[element].iov_len = byte;
    if (!byte)
      tn_iov--;
    if (!tn_iov) {
      tiov[element].iov_len = 1;
      tn_iov++;
    }
    // printf("partitial read %d [%d]\n",tn_iov,tiov[element].iov_len);
  }
  return socketManager.read_vector(fd, &tiov[0], tn_iov);
}
#endif // TEST_PARTIAL_READS

#if TEST_PARTIAL_WRITES
//
// Test code which mimic the network backing up (too little buffering)
//
int
partial_writev(int fd, IOVec * iov, int n_iov, int seq)
{
  int rnd = seq;
  int sum = 0;
  int i = 0;
    for (i = 0; i < n_iov; i++) {
      int l = iov[i].iov_len;
      int r = rand_r((unsigned int *) &rnd);
      if ((r >> 4) & 1) {
        l = ((unsigned int) rand_r((unsigned int *) &rnd)) % iov[i].iov_len;
        if (!l) {
          l = iov[i].iov_len;
        }
      }
      ink_assert(l <= iov[i].iov_len);
      fprintf(stderr, "writing %d: [%d] &%X %d of %d\n", seq, i, iov[i].iov_base, l, iov[i].iov_len);
      int res = socketManager.write(fd, iov[i].iov_base, l);
      if (res < 0) {
        return res;
      }
      sum += res;
      if (res != iov[i].iov_len) {
        return sum;
      }
    }
    return sum;
}
#endif // TEST_PARTIAL_WRITES

////////////////////////////////////////////////////////////////////////
// Global periodic system dump functions
////////////////////////////////////////////////////////////////////////
#ifdef ENABLE_TIME_TRACE
int inmsg_time_dist[TIME_DIST_BUCKETS_SIZE];
int inmsg_events = 0;

int cluster_send_time_dist[TIME_DIST_BUCKETS_SIZE];
int cluster_send_events = 0;
#endif // ENABLE_TIME_TRACE

int time_trace = 0;

void
dump_time_buckets()
{
#ifdef ENABLE_TIME_TRACE
  printf("\nremote ops:\n");
  for (int i = 0; i < TIME_DIST_BUCKETS_SIZE; ++i) {
    printf("%d ", rmt_callback_time_dist[i]);
    rmt_callback_time_dist[i] = 0;
  }
  printf("\nremote lookup ops:\n");
  for (int j = 0; j < TIME_DIST_BUCKETS_SIZE; ++j) {
    printf("%d ", lkrmt_callback_time_dist[j]);
    lkrmt_callback_time_dist[j] = 0;
  }
  printf("\nlocal cache ops:\n");
  for (int k = 0; k < TIME_DIST_BUCKETS_SIZE; ++k) {
    printf("%d ", callback_time_dist[k]);
    callback_time_dist[k] = 0;
  }
  printf("\nphysical cache ops:\n");
  for (int l = 0; l < TIME_DIST_BUCKETS_SIZE; ++l) {
    printf("%d ", cdb_callback_time_dist[l]);
    cdb_callback_time_dist[l] = 0;
  }
  printf("\nin message ops:\n");
  for (int m = 0; m < TIME_DIST_BUCKETS_SIZE; ++m) {
    printf("%d ", inmsg_time_dist[m]);
    inmsg_time_dist[m] = 0;
  }
  printf("\ncluster send time:\n");
  for (int n = 0; n < TIME_DIST_BUCKETS_SIZE; ++n) {
    printf("%d ", cluster_send_time_dist[n]);
    cluster_send_time_dist[n] = 0;
  }
#endif // ENABLE_TIME_TRACE
}

GlobalClusterPeriodicEvent::GlobalClusterPeriodicEvent():Continuation(new_ProxyMutex())
{
  SET_HANDLER((GClusterPEHandler) & GlobalClusterPeriodicEvent::calloutEvent);
}

GlobalClusterPeriodicEvent::~GlobalClusterPeriodicEvent()
{
  _thisCallout->cancel(this);
}

void
GlobalClusterPeriodicEvent::init()
{
  _thisCallout = eventProcessor.schedule_every(this, HRTIME_SECONDS(10), ET_CALL);
}

int
GlobalClusterPeriodicEvent::calloutEvent(Event * e, void *data)
{
  NOWARN_UNUSED(e);
  NOWARN_UNUSED(data);
  if (time_trace) {
    dump_time_buckets();
  }
  clusterProcessor.compute_cluster_mode();
  return EVENT_CONT;
}

// End of ClusterLib.cc
