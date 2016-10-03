/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "HttpLog.h"

#include "nsHttpConnectionMgr.h"
#include "nsHttpConnection.h"
#include "SpdySession31.h"
#include "Http2Session.h"
#include "nsHttpHandler.h"
#include "nsIConsoleService.h"
#include "nsHttpRequestHead.h"
#include "nsServiceManagerUtils.h"
#include "nsSocketTransportService2.h"

namespace mozilla {
namespace net {

void
nsHttpConnectionMgr::PrintDiagnostics()
{
  PostEvent(&nsHttpConnectionMgr::OnMsgPrintDiagnostics, 0, nullptr);
}

void
nsHttpConnectionMgr::OnMsgPrintDiagnostics(int32_t, ARefBase *)
{
  MOZ_ASSERT(PR_GetCurrentThread() == gSocketThread);

  nsCOMPtr<nsIConsoleService> consoleService =
    do_GetService(NS_CONSOLESERVICE_CONTRACTID);
  if (!consoleService)
    return;

  mLogData.AppendPrintf("HTTP Connection Diagnostics\n---------------------\n");
  mLogData.AppendPrintf("IsSpdyEnabled() = %d\n", gHttpHandler->IsSpdyEnabled());
  mLogData.AppendPrintf("MaxSocketCount() = %d\n", gHttpHandler->MaxSocketCount());
  mLogData.AppendPrintf("mNumActiveConns = %d\n", mNumActiveConns);
  mLogData.AppendPrintf("mNumIdleConns = %d\n", mNumIdleConns);

  for (auto iter = mCT.Iter(); !iter.Done(); iter.Next()) {
    nsAutoPtr<nsConnectionEntry>& ent = iter.Data();

    mLogData.AppendPrintf(" ent host = %s hashkey = %s\n",
                          ent->mConnInfo->Origin(), ent->mConnInfo->HashKey().get());
    mLogData.AppendPrintf("   AtActiveConnectionLimit = %d\n",
                          AtActiveConnectionLimit(ent, NS_HTTP_ALLOW_KEEPALIVE));
    mLogData.AppendPrintf("   RestrictConnections = %d\n",
                          RestrictConnections(ent));
    mLogData.AppendPrintf("   Pending Q Length = %u\n",
                          ent->mPendingQ.Length());
    mLogData.AppendPrintf("   Active Conns Length = %u\n",
                          ent->mActiveConns.Length());
    mLogData.AppendPrintf("   Idle Conns Length = %u\n",
                          ent->mIdleConns.Length());
    mLogData.AppendPrintf("   Half Opens Length = %u\n",
                          ent->mHalfOpens.Length());
    mLogData.AppendPrintf("   Coalescing Keys Length = %u\n",
                          ent->mCoalescingKeys.Length());
    mLogData.AppendPrintf("   Spdy using = %d, preferred = %d\n",
                          ent->mUsingSpdy, ent->mInPreferredHash);
    mLogData.AppendPrintf("   pipelinestate = %d penalty = %d\n",
                          ent->mPipelineState, ent->mPipeliningPenalty);

    uint32_t i;
    for (i = 0; i < nsAHttpTransaction::CLASS_MAX; ++i) {
      mLogData.AppendPrintf("   pipeline per class penalty 0x%x %d\n",
                            i, ent->mPipeliningClassPenalty[i]);
    }
    for (i = 0; i < ent->mActiveConns.Length(); ++i) {
      mLogData.AppendPrintf("   :: Active Connection #%u\n", i);
      ent->mActiveConns[i]->PrintDiagnostics(mLogData);
    }
    for (i = 0; i < ent->mIdleConns.Length(); ++i) {
      mLogData.AppendPrintf("   :: Idle Connection #%u\n", i);
      ent->mIdleConns[i]->PrintDiagnostics(mLogData);
    }
    for (i = 0; i < ent->mHalfOpens.Length(); ++i) {
      mLogData.AppendPrintf("   :: Half Open #%u\n", i);
      ent->mHalfOpens[i]->PrintDiagnostics(mLogData);
    }
    for (i = 0; i < ent->mPendingQ.Length(); ++i) {
      mLogData.AppendPrintf("   :: Pending Transaction #%u\n", i);
      ent->mPendingQ[i]->PrintDiagnostics(mLogData);
    }
    for (i = 0; i < ent->mCoalescingKeys.Length(); ++i) {
      mLogData.AppendPrintf("   :: Coalescing Key #%u %s\n",
                            i, ent->mCoalescingKeys[i].get());
    }
  }

  consoleService->LogStringMessage(NS_ConvertUTF8toUTF16(mLogData).Data());
  mLogData.Truncate();
}

void
nsHttpConnectionMgr::nsHalfOpenSocket::PrintDiagnostics(nsCString &log)
{
  log.AppendPrintf("     has connected = %d, isSpeculative = %d\n",
                   HasConnected(), IsSpeculative());

  TimeStamp now = TimeStamp::Now();

  if (mPrimarySynStarted.IsNull())
    log.AppendPrintf("    primary not started\n");
  else
    log.AppendPrintf("    primary started %.2fms ago\n",
                     (now - mPrimarySynStarted).ToMilliseconds());

  if (mBackupSynStarted.IsNull())
    log.AppendPrintf("    backup not started\n");
  else
    log.AppendPrintf("    backup started %.2f ago\n",
                     (now - mBackupSynStarted).ToMilliseconds());

  log.AppendPrintf("    primary transport %d, backup transport %d\n",
                   !!mSocketTransport.get(), !!mBackupTransport.get());
}

void
nsHttpConnection::PrintDiagnostics(nsCString &log)
{
  log.AppendPrintf("    CanDirectlyActivate = %d\n", CanDirectlyActivate());

  log.AppendPrintf("    npncomplete = %d  setupSSLCalled = %d\n",
                   mNPNComplete, mSetupSSLCalled);

  log.AppendPrintf("    spdyVersion = %d  reportedSpdy = %d everspdy = %d\n",
                   mUsingSpdyVersion, mReportedSpdy, mEverUsedSpdy);

  log.AppendPrintf("    iskeepalive = %d  dontReuse = %d isReused = %d\n",
                   IsKeepAlive(), mDontReuse, mIsReused);

  log.AppendPrintf("    mTransaction = %d mSpdySession = %d\n",
                   !!mTransaction.get(), !!mSpdySession.get());

  PRIntervalTime now = PR_IntervalNow();
  log.AppendPrintf("    time since last read = %ums\n",
                   PR_IntervalToMilliseconds(now - mLastReadTime));

  log.AppendPrintf("    max-read/read/written %lld/%lld/%lld\n",
                   mMaxBytesRead, mTotalBytesRead, mTotalBytesWritten);

  log.AppendPrintf("    rtt = %ums\n", PR_IntervalToMilliseconds(mRtt));

  log.AppendPrintf("    idlemonitoring = %d transactionCount=%d\n",
                   mIdleMonitoring, mHttp1xTransactionCount);

  log.AppendPrintf("    supports pipeline = %d classification = 0x%x\n",
                   mSupportsPipelining, mClassification);

  if (mSpdySession)
    mSpdySession->PrintDiagnostics(log);
}

void
SpdySession31::PrintDiagnostics(nsCString &log)
{
  log.AppendPrintf("     ::: SPDY VERSION 3.1\n");
  log.AppendPrintf("     shouldgoaway = %d mClosed = %d CanReuse = %d nextID=0x%X\n",
                   mShouldGoAway, mClosed, CanReuse(), mNextStreamID);

  log.AppendPrintf("     concurrent = %d maxconcurrent = %d\n",
                   mConcurrent, mMaxConcurrent);

  log.AppendPrintf("     roomformorestreams = %d roomformoreconcurrent = %d\n",
                   RoomForMoreStreams(), RoomForMoreConcurrent());

  log.AppendPrintf("     transactionHashCount = %d streamIDHashCount = %d\n",
                   mStreamTransactionHash.Count(),
                   mStreamIDHash.Count());

  log.AppendPrintf("     Queued Stream Size = %d\n", mQueuedStreams.GetSize());

  PRIntervalTime now = PR_IntervalNow();
  log.AppendPrintf("     Ping Threshold = %ums next ping id = 0x%X\n",
                   PR_IntervalToMilliseconds(mPingThreshold),
                   mNextPingID);
  log.AppendPrintf("     Ping Timeout = %ums\n",
                   PR_IntervalToMilliseconds(gHttpHandler->SpdyPingTimeout()));
  log.AppendPrintf("     Idle for Any Activity (ping) = %ums\n",
                   PR_IntervalToMilliseconds(now - mLastReadEpoch));
  log.AppendPrintf("     Idle for Data Activity = %ums\n",
                   PR_IntervalToMilliseconds(now - mLastDataReadEpoch));
  if (mPingSentEpoch)
    log.AppendPrintf("     Ping Outstanding (ping) = %ums, expired = %d\n",
                     PR_IntervalToMilliseconds(now - mPingSentEpoch),
                     now - mPingSentEpoch >= gHttpHandler->SpdyPingTimeout());
  else
    log.AppendPrintf("     No Ping Outstanding\n");
}

void
Http2Session::PrintDiagnostics(nsCString &log)
{
  log.AppendPrintf("     ::: HTTP2\n");
  log.AppendPrintf("     shouldgoaway = %d mClosed = %d CanReuse = %d nextID=0x%X\n",
                   mShouldGoAway, mClosed, CanReuse(), mNextStreamID);

  log.AppendPrintf("     concurrent = %d maxconcurrent = %d\n",
                   mConcurrent, mMaxConcurrent);

  log.AppendPrintf("     roomformorestreams = %d roomformoreconcurrent = %d\n",
                   RoomForMoreStreams(), RoomForMoreConcurrent());

  log.AppendPrintf("     transactionHashCount = %d streamIDHashCount = %d\n",
                   mStreamTransactionHash.Count(),
                   mStreamIDHash.Count());

  log.AppendPrintf("     Queued Stream Size = %d\n", mQueuedStreams.GetSize());

  PRIntervalTime now = PR_IntervalNow();
  log.AppendPrintf("     Ping Threshold = %ums\n",
                   PR_IntervalToMilliseconds(mPingThreshold));
  log.AppendPrintf("     Ping Timeout = %ums\n",
                   PR_IntervalToMilliseconds(gHttpHandler->SpdyPingTimeout()));
  log.AppendPrintf("     Idle for Any Activity (ping) = %ums\n",
                   PR_IntervalToMilliseconds(now - mLastReadEpoch));
  log.AppendPrintf("     Idle for Data Activity = %ums\n",
                   PR_IntervalToMilliseconds(now - mLastDataReadEpoch));
  if (mPingSentEpoch)
    log.AppendPrintf("     Ping Outstanding (ping) = %ums, expired = %d\n",
                     PR_IntervalToMilliseconds(now - mPingSentEpoch),
                     now - mPingSentEpoch >= gHttpHandler->SpdyPingTimeout());
  else
    log.AppendPrintf("     No Ping Outstanding\n");
}

void
nsHttpTransaction::PrintDiagnostics(nsCString &log)
{
  if (!mRequestHead)
    return;

  nsAutoCString requestURI;
  mRequestHead->RequestURI(requestURI);
  log.AppendPrintf("     ::: uri = %s\n", requestURI.get());
  log.AppendPrintf("     caps = 0x%x\n", mCaps);
  log.AppendPrintf("     priority = %d\n", mPriority);
  log.AppendPrintf("     restart count = %u\n", mRestartCount);
  log.AppendPrintf("     classification = 0x%x\n", mClassification);
}

} // namespace net
} // namespace mozilla
