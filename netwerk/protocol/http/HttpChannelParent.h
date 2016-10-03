/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et tw=80 : */

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_HttpChannelParent_h
#define mozilla_net_HttpChannelParent_h

#include "ADivertableParentChannel.h"
#include "nsHttp.h"
#include "mozilla/net/PHttpChannelParent.h"
#include "mozilla/net/NeckoCommon.h"
#include "mozilla/net/NeckoParent.h"
#include "OfflineObserver.h"
#include "nsIObserver.h"
#include "nsIParentRedirectingChannel.h"
#include "nsIProgressEventSink.h"
#include "nsHttpChannel.h"
#include "nsIAuthPromptProvider.h"
#include "mozilla/dom/ipc/IdType.h"
#include "nsIDeprecationWarner.h"

class nsICacheEntry;
class nsIAssociatedContentSecurity;

#define HTTP_CHANNEL_PARENT_IID \
  { 0x982b2372, 0x7aa5, 0x4e8a, \
      { 0xbd, 0x9f, 0x89, 0x74, 0xd7, 0xf0, 0x58, 0xeb } }

namespace mozilla {

namespace dom{
class TabParent;
class PBrowserOrId;
} // namespace dom

namespace net {

class HttpChannelParentListener;
class ChannelEventQueue;

// Note: nsIInterfaceRequestor must be the first base so that do_QueryObject()
// works correctly on this object, as it's needed to compute a void* pointing to
// the beginning of this object.

class HttpChannelParent final : public nsIInterfaceRequestor
                              , public PHttpChannelParent
                              , public nsIParentRedirectingChannel
                              , public nsIProgressEventSink
                              , public ADivertableParentChannel
                              , public nsIAuthPromptProvider
                              , public nsIDeprecationWarner
                              , public DisconnectableParent
                              , public HttpChannelSecurityWarningReporter
{
  virtual ~HttpChannelParent();

public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSISTREAMLISTENER
  NS_DECL_NSIPARENTCHANNEL
  NS_DECL_NSIPARENTREDIRECTINGCHANNEL
  NS_DECL_NSIPROGRESSEVENTSINK
  NS_DECL_NSIINTERFACEREQUESTOR
  NS_DECL_NSIAUTHPROMPTPROVIDER
  NS_DECL_NSIDEPRECATIONWARNER

  NS_DECLARE_STATIC_IID_ACCESSOR(HTTP_CHANNEL_PARENT_IID)

  HttpChannelParent(const dom::PBrowserOrId& iframeEmbedding,
                    nsILoadContext* aLoadContext,
                    PBOverrideStatus aStatus);

  bool Init(const HttpChannelCreationArgs& aOpenArgs);

  // ADivertableParentChannel functions.
  void DivertTo(nsIStreamListener *aListener) override;
  nsresult SuspendForDiversion() override;
  nsresult SuspendMessageDiversion() override;
  nsresult ResumeMessageDiversion() override;

  // Calls OnStartRequest for "DivertTo" listener, then notifies child channel
  // that it should divert OnDataAvailable and OnStopRequest calls to this
  // parent channel.
  void StartDiversion();

  // Handles calling OnStart/Stop if there are errors during diversion.
  // Called asynchronously from FailDiversion.
  void NotifyDiversionFailed(nsresult aErrorCode, bool aSkipResume = true);

  // Forwarded to nsHttpChannel::SetApplyConversion.
  void SetApplyConversion(bool aApplyConversion) {
    if (mChannel) {
      mChannel->SetApplyConversion(aApplyConversion);
    }
  }

protected:
  // used to connect redirected-to channel in parent with just created
  // ChildChannel.  Used during redirects.
  bool ConnectChannel(const uint32_t& channelId, const bool& shouldIntercept);

  bool DoAsyncOpen(const URIParams&           uri,
                   const OptionalURIParams&   originalUri,
                   const OptionalURIParams&   docUri,
                   const OptionalURIParams&   referrerUri,
                   const uint32_t&            referrerPolicy,
                   const OptionalURIParams&   internalRedirectUri,
                   const OptionalURIParams&   topWindowUri,
                   const uint32_t&            loadFlags,
                   const RequestHeaderTuples& requestHeaders,
                   const nsCString&           requestMethod,
                   const OptionalInputStreamParams& uploadStream,
                   const bool&                uploadStreamHasHeaders,
                   const uint16_t&            priority,
                   const uint32_t&            classOfService,
                   const uint8_t&             redirectionLimit,
                   const bool&                allowPipelining,
                   const bool&                allowSTS,
                   const uint32_t&            thirdPartyFlags,
                   const bool&                doResumeAt,
                   const uint64_t&            startPos,
                   const nsCString&           entityID,
                   const bool&                chooseApplicationCache,
                   const nsCString&           appCacheClientID,
                   const bool&                allowSpdy,
                   const bool&                allowAltSvc,
                   const OptionalFileDescriptorSet& aFds,
                   const OptionalLoadInfoArgs& aLoadInfoArgs,
                   const OptionalHttpResponseHead& aSynthesizedResponseHead,
                   const nsCString&           aSecurityInfoSerialization,
                   const uint32_t&            aCacheKey,
                   const nsCString&           aRequestContextID,
                   const OptionalCorsPreflightArgs& aCorsPreflightArgs,
                   const uint32_t&            aInitialRwin,
                   const bool&                aBlockAuthPrompt,
                   const bool&                aSuspendAfterSynthesizeResponse,
                   const bool&                aAllowStaleCacheContent,
                   const nsCString&           aContentTypeHint,
                   const nsCString&           aChannelId);

  virtual bool RecvSetPriority(const uint16_t& priority) override;
  virtual bool RecvSetClassOfService(const uint32_t& cos) override;
  virtual bool RecvSetCacheTokenCachedCharset(const nsCString& charset) override;
  virtual bool RecvSuspend() override;
  virtual bool RecvResume() override;
  virtual bool RecvCancel(const nsresult& status) override;
  virtual bool RecvRedirect2Verify(const nsresult& result,
                                   const RequestHeaderTuples& changedHeaders,
                                   const uint32_t& loadFlags,
                                   const OptionalURIParams& apiRedirectUri,
                                   const OptionalCorsPreflightArgs& aCorsPreflightArgs) override;
  virtual bool RecvUpdateAssociatedContentSecurity(const int32_t& broken,
                                                   const int32_t& no) override;
  virtual bool RecvDocumentChannelCleanup() override;
  virtual bool RecvMarkOfflineCacheEntryAsForeign() override;
  virtual bool RecvDivertOnDataAvailable(const nsCString& data,
                                         const uint64_t& offset,
                                         const uint32_t& count) override;
  virtual bool RecvDivertOnStopRequest(const nsresult& statusCode) override;
  virtual bool RecvDivertComplete() override;
  virtual bool RecvRemoveCorsPreflightCacheEntry(const URIParams& uri,
                                                 const mozilla::ipc::PrincipalInfo& requestingPrincipal) override;
  virtual void ActorDestroy(ActorDestroyReason why) override;

  // Supporting function for ADivertableParentChannel.
  nsresult ResumeForDiversion();

  // Asynchronously calls NotifyDiversionFailed.
  void FailDiversion(nsresult aErrorCode, bool aSkipResume = true);

  friend class HttpChannelParentListener;
  RefPtr<mozilla::dom::TabParent> mTabParent;

  void OfflineDisconnect() override;
  uint32_t GetAppId() override;

  nsresult ReportSecurityMessage(const nsAString& aMessageTag,
                                 const nsAString& aMessageCategory) override;

  // Calls SendDeleteSelf and sets mIPCClosed to true because we should not
  // send any more messages after that. Bug 1274886
  bool DoSendDeleteSelf();

private:
  void UpdateAndSerializeSecurityInfo(nsACString& aSerializedSecurityInfoOut);

  void DivertOnDataAvailable(const nsCString& data,
                             const uint64_t& offset,
                             const uint32_t& count);
  void DivertOnStopRequest(const nsresult& statusCode);
  void DivertComplete();
  void MaybeFlushPendingDiversion();
  void ResponseSynthesized();

  friend class DivertDataAvailableEvent;
  friend class DivertStopRequestEvent;
  friend class DivertCompleteEvent;

  RefPtr<nsHttpChannel>       mChannel;
  nsCOMPtr<nsICacheEntry>       mCacheEntry;
  nsCOMPtr<nsIAssociatedContentSecurity>  mAssociatedContentSecurity;
  bool mIPCClosed;                // PHttpChannel actor has been Closed()

  nsCOMPtr<nsIChannel> mRedirectChannel;
  nsCOMPtr<nsIAsyncVerifyRedirectCallback> mRedirectCallback;

  nsAutoPtr<class nsHttpChannel::OfflineCacheEntryAsForeignMarker> mOfflineForeignMarker;

  // state for combining OnStatus/OnProgress with OnDataAvailable
  // into one IPDL call to child.
  nsresult mStoredStatus;
  int64_t mStoredProgress;
  int64_t mStoredProgressMax;

  bool mSentRedirect1Begin          : 1;
  bool mSentRedirect1BeginFailed    : 1;
  bool mReceivedRedirect2Verify     : 1;

  RefPtr<OfflineObserver> mObserver;

  PBOverrideStatus mPBOverride;

  nsCOMPtr<nsILoadContext> mLoadContext;
  RefPtr<nsHttpHandler>  mHttpHandler;

  RefPtr<HttpChannelParentListener> mParentListener;
  // The listener we are diverting to or will divert to if mPendingDiversion
  // is set.
  nsCOMPtr<nsIStreamListener> mDivertListener;
  // Set to the canceled status value if the main channel was canceled.
  nsresult mStatus;
  // Indicates that diversion has been requested, but we could not start it
  // yet because the channel is still being opened with a synthesized response.
  bool mPendingDiversion;
  // Once set, no OnStart/OnData/OnStop calls should be accepted; conversely, it
  // must be set when RecvDivertOnData/~DivertOnStop/~DivertComplete are
  // received from the child channel.
  bool mDivertingFromChild;

  // Set if OnStart|StopRequest was called during a diversion from the child.
  bool mDivertedOnStartRequest;

  bool mSuspendedForDiversion;

  // Set if this channel should be suspended after synthesizing a response.
  bool mSuspendAfterSynthesizeResponse;
  // Set if this channel will synthesize its response.
  bool mWillSynthesizeResponse;

  dom::TabId mNestedFrameId;

  RefPtr<ChannelEventQueue> mEventQ;
};

NS_DEFINE_STATIC_IID_ACCESSOR(HttpChannelParent,
                              HTTP_CHANNEL_PARENT_IID)

} // namespace net
} // namespace mozilla

#endif // mozilla_net_HttpChannelParent_h
