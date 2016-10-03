/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:set ts=4 sw=4 sts=4 et: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsViewSourceChannel.h"
#include "nsIIOService.h"
#include "nsMimeTypes.h"
#include "nsNetUtil.h"
#include "nsContentUtils.h"
#include "nsIHttpHeaderVisitor.h"
#include "nsContentSecurityManager.h"
#include "nsNullPrincipal.h"
#include "nsServiceManagerUtils.h"
#include "nsIInputStreamChannel.h"
#include "mozilla/DebugOnly.h"

NS_IMPL_ADDREF(nsViewSourceChannel)
NS_IMPL_RELEASE(nsViewSourceChannel)
/*
  This QI uses NS_INTERFACE_MAP_ENTRY_CONDITIONAL to check for
  non-nullness of mHttpChannel, mCachingChannel, and mUploadChannel.
*/
NS_INTERFACE_MAP_BEGIN(nsViewSourceChannel)
    NS_INTERFACE_MAP_ENTRY(nsIViewSourceChannel)
    NS_INTERFACE_MAP_ENTRY(nsIStreamListener)
    NS_INTERFACE_MAP_ENTRY(nsIRequestObserver)
    NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIHttpChannel, mHttpChannel)
    NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIHttpChannelInternal, mHttpChannelInternal)
    NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsICachingChannel, mCachingChannel)
    NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsICacheInfoChannel, mCacheInfoChannel)
    NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIApplicationCacheChannel, mApplicationCacheChannel)
    NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIUploadChannel, mUploadChannel)
    NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIFormPOSTActionChannel, mPostChannel)
    NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsIRequest, nsIViewSourceChannel)
    NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsIChannel, nsIViewSourceChannel)
    NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIViewSourceChannel)
NS_INTERFACE_MAP_END

nsresult
nsViewSourceChannel::Init(nsIURI* uri)
{
    mOriginalURI = uri;

    nsAutoCString path;
    nsresult rv = uri->GetPath(path);
    if (NS_FAILED(rv))
      return rv;

    nsCOMPtr<nsIIOService> pService(do_GetIOService(&rv));
    if (NS_FAILED(rv)) return rv;

    nsAutoCString scheme;
    rv = pService->ExtractScheme(path, scheme);
    if (NS_FAILED(rv))
      return rv;

    // prevent viewing source of javascript URIs (see bug 204779)
    if (scheme.LowerCaseEqualsLiteral("javascript")) {
      NS_WARNING("blocking view-source:javascript:");
      return NS_ERROR_INVALID_ARG;
    }

    // This function is called from within nsViewSourceHandler::NewChannel2
    // and sets the right loadInfo right after returning from this function.
    // Until then we follow the principal of least privilege and use
    // nullPrincipal as the loadingPrincipal and the least permissive
    // securityflag.
    nsCOMPtr<nsIPrincipal> nullPrincipal = nsNullPrincipal::Create();

    rv = pService->NewChannel2(path,
                               nullptr, // aOriginCharset
                               nullptr, // aCharSet
                               nullptr, // aLoadingNode
                               nullPrincipal,
                               nullptr, // aTriggeringPrincipal
                               nsILoadInfo::SEC_REQUIRE_SAME_ORIGIN_DATA_IS_BLOCKED,
                               nsIContentPolicy::TYPE_OTHER,
                               getter_AddRefs(mChannel));
    NS_ENSURE_SUCCESS(rv, rv);

    mIsSrcdocChannel = false;

    mChannel->SetOriginalURI(mOriginalURI);
    mHttpChannel = do_QueryInterface(mChannel);
    mHttpChannelInternal = do_QueryInterface(mChannel);
    mCachingChannel = do_QueryInterface(mChannel);
    mCacheInfoChannel = do_QueryInterface(mChannel);
    mApplicationCacheChannel = do_QueryInterface(mChannel);
    mUploadChannel = do_QueryInterface(mChannel);
    mPostChannel = do_QueryInterface(mChannel);
    
    return NS_OK;
}

nsresult
nsViewSourceChannel::InitSrcdoc(nsIURI* aURI,
                                nsIURI* aBaseURI,
                                const nsAString &aSrcdoc,
                                nsILoadInfo* aLoadInfo)
{
    nsresult rv;

    nsCOMPtr<nsIURI> inStreamURI;
    // Need to strip view-source: from the URI.  Hardcoded to
    // about:srcdoc as this is the only permissible URI for srcdoc 
    // loads
    rv = NS_NewURI(getter_AddRefs(inStreamURI),
                   NS_LITERAL_STRING("about:srcdoc"));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = NS_NewInputStreamChannelInternal(getter_AddRefs(mChannel),
                                          inStreamURI,
                                          aSrcdoc,
                                          NS_LITERAL_CSTRING("text/html"),
                                          aLoadInfo,
                                          true);

    NS_ENSURE_SUCCESS(rv, rv);
    mOriginalURI = aURI;
    mIsSrcdocChannel = true;

    mChannel->SetOriginalURI(mOriginalURI);
    mHttpChannel = do_QueryInterface(mChannel);
    mHttpChannelInternal = do_QueryInterface(mChannel);
    mCachingChannel = do_QueryInterface(mChannel);
    mCacheInfoChannel = do_QueryInterface(mChannel);
    mApplicationCacheChannel = do_QueryInterface(mChannel);
    mUploadChannel = do_QueryInterface(mChannel);

    nsCOMPtr<nsIInputStreamChannel> isc = do_QueryInterface(mChannel);
    MOZ_ASSERT(isc);
    isc->SetBaseURI(aBaseURI);
    return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsIRequest methods:

NS_IMETHODIMP
nsViewSourceChannel::GetName(nsACString &result)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsViewSourceChannel::GetTransferSize(uint64_t *aTransferSize)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsViewSourceChannel::GetDecodedBodySize(uint64_t *aDecodedBodySize)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsViewSourceChannel::GetEncodedBodySize(uint64_t *aEncodedBodySize)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsViewSourceChannel::IsPending(bool *result)
{
    NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

    return mChannel->IsPending(result);
}

NS_IMETHODIMP
nsViewSourceChannel::GetStatus(nsresult *status)
{
    NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

    return mChannel->GetStatus(status);
}

NS_IMETHODIMP
nsViewSourceChannel::Cancel(nsresult status)
{
    NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

    return mChannel->Cancel(status);
}

NS_IMETHODIMP
nsViewSourceChannel::Suspend(void)
{
    NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

    return mChannel->Suspend();
}

NS_IMETHODIMP
nsViewSourceChannel::Resume(void)
{
    NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

    return mChannel->Resume();
}

////////////////////////////////////////////////////////////////////////////////
// nsIChannel methods:

NS_IMETHODIMP
nsViewSourceChannel::GetOriginalURI(nsIURI* *aURI)
{
    NS_ASSERTION(aURI, "Null out param!");
    *aURI = mOriginalURI;
    NS_ADDREF(*aURI);
    return NS_OK;
}

NS_IMETHODIMP
nsViewSourceChannel::SetOriginalURI(nsIURI* aURI)
{
    NS_ENSURE_ARG_POINTER(aURI);
    mOriginalURI = aURI;
    return NS_OK;
}

NS_IMETHODIMP
nsViewSourceChannel::GetURI(nsIURI* *aURI)
{
    NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

    nsCOMPtr<nsIURI> uri;
    nsresult rv = mChannel->GetURI(getter_AddRefs(uri));
    if (NS_FAILED(rv))
      return rv;

    // protect ourselves against broken channel implementations
    if (!uri) {
      NS_ERROR("inner channel returned NS_OK and a null URI");
      return NS_ERROR_UNEXPECTED;
    }

    nsAutoCString spec;
    uri->GetSpec(spec);

    /* XXX Gross hack -- NS_NewURI goes into an infinite loop on
       non-flat specs.  See bug 136980 */
    return NS_NewURI(aURI, nsAutoCString(NS_LITERAL_CSTRING("view-source:")+spec), nullptr);
}

NS_IMETHODIMP
nsViewSourceChannel::Open(nsIInputStream **_retval)
{
    NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

    nsresult rv = NS_OK;
    nsCOMPtr<nsILoadInfo> loadInfo = mChannel->GetLoadInfo();
    if (loadInfo && loadInfo->GetEnforceSecurity()) {
        mChannel->Open2(_retval);
    }
    else {
        mChannel->Open(_retval);
    }
    if (NS_SUCCEEDED(rv)) {
        mOpened = true;
    }
    return rv;
}

NS_IMETHODIMP
nsViewSourceChannel::Open2(nsIInputStream** aStream)
{
    NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);
    nsCOMPtr<nsILoadInfo> loadInfo = mChannel->GetLoadInfo();
    if(!loadInfo) {
        MOZ_ASSERT(loadInfo, "can not enforce security without loadInfo");
        return NS_ERROR_UNEXPECTED;
    }
    // setting the flag on the loadInfo indicates that the underlying
    // channel will be openend using Open2() and hence performs
    // the necessary security checks.
    loadInfo->SetEnforceSecurity(true);
    return Open(aStream);
}

NS_IMETHODIMP
nsViewSourceChannel::AsyncOpen(nsIStreamListener *aListener, nsISupports *ctxt)
{
#ifdef DEBUG
    {
    nsCOMPtr<nsILoadInfo> loadInfo = mChannel->GetLoadInfo();
    MOZ_ASSERT(!loadInfo || loadInfo->GetSecurityMode() == 0 ||
               loadInfo->GetEnforceSecurity(),
               "security flags in loadInfo but asyncOpen2() not called");
    }
#endif

    NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

    mListener = aListener;

    /*
     * We want to add ourselves to the loadgroup before opening
     * mChannel, since we want to make sure we're in the loadgroup
     * when mChannel finishes and fires OnStopRequest()
     */
    
    nsCOMPtr<nsILoadGroup> loadGroup;
    mChannel->GetLoadGroup(getter_AddRefs(loadGroup));
    if (loadGroup)
        loadGroup->AddRequest(static_cast<nsIViewSourceChannel*>
                                         (this), nullptr);
    
    nsresult rv = NS_OK;
    nsCOMPtr<nsILoadInfo> loadInfo = mChannel->GetLoadInfo();
    if (loadInfo && loadInfo->GetEnforceSecurity()) {
        rv = mChannel->AsyncOpen2(this);
    }
    else {
        rv = mChannel->AsyncOpen(this, ctxt);
    }

    if (NS_FAILED(rv) && loadGroup)
        loadGroup->RemoveRequest(static_cast<nsIViewSourceChannel*>
                                            (this),
                                 nullptr, rv);

    if (NS_SUCCEEDED(rv)) {
        mOpened = true;
    }
    
    return rv;
}

NS_IMETHODIMP
nsViewSourceChannel::AsyncOpen2(nsIStreamListener *aListener)
{
  nsCOMPtr<nsILoadInfo> loadInfo = mChannel->GetLoadInfo();
  if(!loadInfo) {
    MOZ_ASSERT(loadInfo, "can not enforce security without loadInfo");
    return NS_ERROR_UNEXPECTED;
  }
  // setting the flag on the loadInfo indicates that the underlying
  // channel will be openend using AsyncOpen2() and hence performs
  // the necessary security checks.
  loadInfo->SetEnforceSecurity(true);
  return AsyncOpen(aListener, nullptr);
}
/*
 * Both the view source channel and mChannel are added to the
 * loadgroup.  There should never be more than one request in the
 * loadgroup that has LOAD_DOCUMENT_URI set.  The one that has this
 * flag set is the request whose URI is used to refetch the document,
 * so it better be the viewsource channel.
 *
 * Therefore, we need to make sure that
 * 1) The load flags on mChannel _never_ include LOAD_DOCUMENT_URI
 * 2) The load flags on |this| include LOAD_DOCUMENT_URI when it was
 *    set via SetLoadFlags (mIsDocument keeps track of this flag).
 */

NS_IMETHODIMP
nsViewSourceChannel::GetLoadFlags(uint32_t *aLoadFlags)
{
    NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

    nsresult rv = mChannel->GetLoadFlags(aLoadFlags);
    if (NS_FAILED(rv))
      return rv;

    // This should actually be just LOAD_DOCUMENT_URI but the win32 compiler
    // fails to deal due to amiguous inheritance.  nsIChannel::LOAD_DOCUMENT_URI
    // also fails; the Win32 compiler thinks that's supposed to be a method.
    if (mIsDocument)
      *aLoadFlags |= ::nsIChannel::LOAD_DOCUMENT_URI;

    return rv;
}

NS_IMETHODIMP
nsViewSourceChannel::SetLoadFlags(uint32_t aLoadFlags)
{
    NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

    // "View source" always wants the currently cached content.
    // We also want to have _this_ channel, not mChannel to be the
    // 'document' channel in the loadgroup. 
 
    // These should actually be just LOAD_FROM_CACHE and LOAD_DOCUMENT_URI but
    // the win32 compiler fails to deal due to amiguous inheritance.
    // nsIChannel::LOAD_DOCUMENT_URI/nsIRequest::LOAD_FROM_CACHE also fails; the
    // Win32 compiler thinks that's supposed to be a method.
    mIsDocument = (aLoadFlags & ::nsIChannel::LOAD_DOCUMENT_URI) ? true : false;

    nsresult rv = mChannel->SetLoadFlags((aLoadFlags |
                                          ::nsIRequest::LOAD_FROM_CACHE) &
                                          ~::nsIChannel::LOAD_DOCUMENT_URI);
    if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
    }

    if (mHttpChannel) {
       rv = mHttpChannel->SetIsMainDocumentChannel(aLoadFlags & ::nsIChannel::LOAD_DOCUMENT_URI);
       MOZ_ASSERT(NS_SUCCEEDED(rv));
    }

    return NS_OK;
}

NS_IMETHODIMP
nsViewSourceChannel::GetContentType(nsACString &aContentType) 
{
    NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

    aContentType.Truncate();

    if (mContentType.IsEmpty())
    {
        // Get the current content type
        nsresult rv;
        nsAutoCString contentType;
        rv = mChannel->GetContentType(contentType);
        if (NS_FAILED(rv)) return rv;

        // If we don't know our type, just say so.  The unknown
        // content decoder will then kick in automatically, and it
        // will call our SetOriginalContentType method instead of our
        // SetContentType method to set the type it determines.
        if (!contentType.Equals(UNKNOWN_CONTENT_TYPE)) {
          contentType = VIEWSOURCE_CONTENT_TYPE;
        }

        mContentType = contentType;
    }

    aContentType = mContentType;
    return NS_OK;
}

NS_IMETHODIMP
nsViewSourceChannel::SetContentType(const nsACString &aContentType)
{
    // Our GetContentType() currently returns VIEWSOURCE_CONTENT_TYPE
    //
    // However, during the parsing phase the parser calls our
    // channel's GetContentType(). Returning the string above trips up
    // the parser. In order to avoid messy changes and not to have the
    // parser depend on nsIViewSourceChannel Vidur proposed the
    // following solution:
    //
    // The ViewSourceChannel initially returns a content type of
    // VIEWSOURCE_CONTENT_TYPE.  Based on this type decisions to
    // create a viewer for doing a view source are made.  After the
    // viewer is created, nsLayoutDLF::CreateInstance() calls this
    // SetContentType() with the original content type.  When it's
    // time for the parser to find out the content type it will call
    // our channel's GetContentType() and it will get the original
    // content type, such as, text/html and everything is kosher from
    // then on.

    if (!mOpened) {
        // We do not take hints
        return NS_ERROR_NOT_AVAILABLE;
    }
    
    mContentType = aContentType;
    return NS_OK;
}

NS_IMETHODIMP
nsViewSourceChannel::GetContentCharset(nsACString &aContentCharset)
{
    NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

    return mChannel->GetContentCharset(aContentCharset);
}

NS_IMETHODIMP
nsViewSourceChannel::SetContentCharset(const nsACString &aContentCharset)
{
    NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

    return mChannel->SetContentCharset(aContentCharset);
}

// We don't forward these methods becacuse content-disposition isn't whitelisted
// (see GetResponseHeader/VisitResponseHeaders).
NS_IMETHODIMP
nsViewSourceChannel::GetContentDisposition(uint32_t *aContentDisposition)
{
    return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
nsViewSourceChannel::SetContentDisposition(uint32_t aContentDisposition)
{
    return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
nsViewSourceChannel::GetContentDispositionFilename(nsAString &aContentDispositionFilename)
{
    return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
nsViewSourceChannel::SetContentDispositionFilename(const nsAString &aContentDispositionFilename)
{
    return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
nsViewSourceChannel::GetContentDispositionHeader(nsACString &aContentDispositionHeader)
{
    return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
nsViewSourceChannel::GetContentLength(int64_t *aContentLength)
{
    NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

    return mChannel->GetContentLength(aContentLength);
}

NS_IMETHODIMP
nsViewSourceChannel::SetContentLength(int64_t aContentLength)
{
    NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

    return mChannel->SetContentLength(aContentLength);
}

NS_IMETHODIMP
nsViewSourceChannel::GetLoadGroup(nsILoadGroup* *aLoadGroup)
{
    NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

    return mChannel->GetLoadGroup(aLoadGroup);
}

NS_IMETHODIMP
nsViewSourceChannel::SetLoadGroup(nsILoadGroup* aLoadGroup)
{
    NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

    return mChannel->SetLoadGroup(aLoadGroup);
}

NS_IMETHODIMP
nsViewSourceChannel::GetOwner(nsISupports* *aOwner)
{
    NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

    return mChannel->GetOwner(aOwner);
}

NS_IMETHODIMP
nsViewSourceChannel::SetOwner(nsISupports* aOwner)
{
    NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

    return mChannel->SetOwner(aOwner);
}

NS_IMETHODIMP
nsViewSourceChannel::GetLoadInfo(nsILoadInfo* *aLoadInfo)
{
    NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

    return mChannel->GetLoadInfo(aLoadInfo);
}

NS_IMETHODIMP
nsViewSourceChannel::SetLoadInfo(nsILoadInfo* aLoadInfo)
{
    NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

    return mChannel->SetLoadInfo(aLoadInfo);
}

NS_IMETHODIMP
nsViewSourceChannel::GetNotificationCallbacks(nsIInterfaceRequestor* *aNotificationCallbacks)
{
    NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

    return mChannel->GetNotificationCallbacks(aNotificationCallbacks);
}

NS_IMETHODIMP
nsViewSourceChannel::SetNotificationCallbacks(nsIInterfaceRequestor* aNotificationCallbacks)
{
    NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

    return mChannel->SetNotificationCallbacks(aNotificationCallbacks);
}

NS_IMETHODIMP 
nsViewSourceChannel::GetSecurityInfo(nsISupports * *aSecurityInfo)
{
    NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

    return mChannel->GetSecurityInfo(aSecurityInfo);
}

// nsIViewSourceChannel methods
NS_IMETHODIMP
nsViewSourceChannel::GetOriginalContentType(nsACString &aContentType) 
{
    NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

    return mChannel->GetContentType(aContentType);
}

NS_IMETHODIMP
nsViewSourceChannel::SetOriginalContentType(const nsACString &aContentType)
{
    NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

    // clear our cached content-type value
    mContentType.Truncate();

    return mChannel->SetContentType(aContentType);
}

NS_IMETHODIMP
nsViewSourceChannel::GetIsSrcdocChannel(bool* aIsSrcdocChannel)
{
    *aIsSrcdocChannel = mIsSrcdocChannel;
    return NS_OK;
}

NS_IMETHODIMP
nsViewSourceChannel::GetBaseURI(nsIURI** aBaseURI)
{
  if (mIsSrcdocChannel) {
    nsCOMPtr<nsIInputStreamChannel> isc = do_QueryInterface(mChannel);
    if (isc) {
      return isc->GetBaseURI(aBaseURI);
    }
  }
  *aBaseURI = mBaseURI;
  NS_IF_ADDREF(*aBaseURI);
  return NS_OK;
}

NS_IMETHODIMP
nsViewSourceChannel::SetBaseURI(nsIURI* aBaseURI)
{
  mBaseURI = aBaseURI;
  return NS_OK;
}

NS_IMETHODIMP
nsViewSourceChannel::GetProtocolVersion(nsACString& aProtocolVersion)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

// nsIRequestObserver methods
NS_IMETHODIMP
nsViewSourceChannel::OnStartRequest(nsIRequest *aRequest, nsISupports *aContext)
{
    NS_ENSURE_TRUE(mListener, NS_ERROR_FAILURE);
    // The channel may have gotten redirected... Time to update our info
    mChannel = do_QueryInterface(aRequest);
    mHttpChannel = do_QueryInterface(aRequest);
    mCachingChannel = do_QueryInterface(aRequest);
    mCacheInfoChannel = do_QueryInterface(mChannel);
    mUploadChannel = do_QueryInterface(aRequest);
    
    return mListener->OnStartRequest(static_cast<nsIViewSourceChannel*>
                                                (this),
                                     aContext);
}


NS_IMETHODIMP
nsViewSourceChannel::OnStopRequest(nsIRequest *aRequest, nsISupports* aContext,
                               nsresult aStatus)
{
    NS_ENSURE_TRUE(mListener, NS_ERROR_FAILURE);
    if (mChannel)
    {
        nsCOMPtr<nsILoadGroup> loadGroup;
        mChannel->GetLoadGroup(getter_AddRefs(loadGroup));
        if (loadGroup)
        {
            loadGroup->RemoveRequest(static_cast<nsIViewSourceChannel*>
                                                (this),
                                     nullptr, aStatus);
        }
    }
    return mListener->OnStopRequest(static_cast<nsIViewSourceChannel*>
                                               (this),
                                    aContext, aStatus);
}


// nsIStreamListener methods
NS_IMETHODIMP
nsViewSourceChannel::OnDataAvailable(nsIRequest *aRequest, nsISupports* aContext,
                                     nsIInputStream *aInputStream,
                                     uint64_t aSourceOffset,
                                     uint32_t aLength) 
{
    NS_ENSURE_TRUE(mListener, NS_ERROR_FAILURE);
    return mListener->OnDataAvailable(static_cast<nsIViewSourceChannel*>
                                                 (this),
                                      aContext, aInputStream,
                                      aSourceOffset, aLength);
}


// nsIHttpChannel methods

// We want to forward most of nsIHttpChannel over to mHttpChannel, but we want
// to override GetRequestHeader and VisitHeaders. The reason is that we don't
// want various headers like Link: and Refresh: applying to view-source.
NS_IMETHODIMP
nsViewSourceChannel::GetChannelId(nsACString& aChannelId)
{
  return !mHttpChannel ? NS_ERROR_NULL_POINTER :
      mHttpChannel->GetChannelId(aChannelId);
}

NS_IMETHODIMP
nsViewSourceChannel::SetChannelId(const nsACString& aChannelId)
{
  return !mHttpChannel ? NS_ERROR_NULL_POINTER :
      mHttpChannel->SetChannelId(aChannelId);
}

NS_IMETHODIMP
nsViewSourceChannel::GetRequestMethod(nsACString & aRequestMethod)
{
    return !mHttpChannel ? NS_ERROR_NULL_POINTER :
        mHttpChannel->GetRequestMethod(aRequestMethod);
}

NS_IMETHODIMP
nsViewSourceChannel::SetRequestMethod(const nsACString & aRequestMethod)
{
    return !mHttpChannel ? NS_ERROR_NULL_POINTER :
        mHttpChannel->SetRequestMethod(aRequestMethod);
}

NS_IMETHODIMP
nsViewSourceChannel::GetReferrer(nsIURI * *aReferrer)
{
    return !mHttpChannel ? NS_ERROR_NULL_POINTER :
        mHttpChannel->GetReferrer(aReferrer);
}

NS_IMETHODIMP
nsViewSourceChannel::SetReferrer(nsIURI * aReferrer)
{
    return !mHttpChannel ? NS_ERROR_NULL_POINTER :
        mHttpChannel->SetReferrer(aReferrer);
}

NS_IMETHODIMP
nsViewSourceChannel::GetReferrerPolicy(uint32_t *aReferrerPolicy)
{
    return !mHttpChannel ? NS_ERROR_NULL_POINTER :
        mHttpChannel->GetReferrerPolicy(aReferrerPolicy);
}

NS_IMETHODIMP
nsViewSourceChannel::SetReferrerWithPolicy(nsIURI * aReferrer,
                                           uint32_t aReferrerPolicy)
{
    return !mHttpChannel ? NS_ERROR_NULL_POINTER :
        mHttpChannel->SetReferrerWithPolicy(aReferrer, aReferrerPolicy);
}

NS_IMETHODIMP
nsViewSourceChannel::GetRequestHeader(const nsACString & aHeader,
                                      nsACString & aValue)
{
    aValue.Truncate();
    return !mHttpChannel ? NS_ERROR_NULL_POINTER :
        mHttpChannel->GetRequestHeader(aHeader, aValue);
}

NS_IMETHODIMP
nsViewSourceChannel::SetRequestHeader(const nsACString & aHeader,
                                      const nsACString & aValue,
                                      bool aMerge)
{
    return !mHttpChannel ? NS_ERROR_NULL_POINTER :
        mHttpChannel->SetRequestHeader(aHeader, aValue, aMerge);
}

NS_IMETHODIMP
nsViewSourceChannel::SetEmptyRequestHeader(const nsACString & aHeader)
{
    return !mHttpChannel ? NS_ERROR_NULL_POINTER :
        mHttpChannel->SetEmptyRequestHeader(aHeader);
}

NS_IMETHODIMP
nsViewSourceChannel::VisitRequestHeaders(nsIHttpHeaderVisitor *aVisitor)
{
    return !mHttpChannel ? NS_ERROR_NULL_POINTER :
        mHttpChannel->VisitRequestHeaders(aVisitor);
}

NS_IMETHODIMP
nsViewSourceChannel::VisitNonDefaultRequestHeaders(nsIHttpHeaderVisitor *aVisitor)
{
    return !mHttpChannel ? NS_ERROR_NULL_POINTER :
        mHttpChannel->VisitNonDefaultRequestHeaders(aVisitor);
}

NS_IMETHODIMP
nsViewSourceChannel::GetAllowPipelining(bool *aAllowPipelining)
{
    return !mHttpChannel ? NS_ERROR_NULL_POINTER :
        mHttpChannel->GetAllowPipelining(aAllowPipelining);
}

NS_IMETHODIMP
nsViewSourceChannel::SetAllowPipelining(bool aAllowPipelining)
{
    return !mHttpChannel ? NS_ERROR_NULL_POINTER :
        mHttpChannel->SetAllowPipelining(aAllowPipelining);
}

NS_IMETHODIMP
nsViewSourceChannel::GetAllowSTS(bool *aAllowSTS)
{
    return !mHttpChannel ? NS_ERROR_NULL_POINTER :
        mHttpChannel->GetAllowSTS(aAllowSTS);
}

NS_IMETHODIMP
nsViewSourceChannel::SetAllowSTS(bool aAllowSTS)
{
    return !mHttpChannel ? NS_ERROR_NULL_POINTER :
        mHttpChannel->SetAllowSTS(aAllowSTS);
}

NS_IMETHODIMP
nsViewSourceChannel::GetRedirectionLimit(uint32_t *aRedirectionLimit)
{
    return !mHttpChannel ? NS_ERROR_NULL_POINTER :
        mHttpChannel->GetRedirectionLimit(aRedirectionLimit);
}

NS_IMETHODIMP
nsViewSourceChannel::SetRedirectionLimit(uint32_t aRedirectionLimit)
{
    return !mHttpChannel ? NS_ERROR_NULL_POINTER :
        mHttpChannel->SetRedirectionLimit(aRedirectionLimit);
}

NS_IMETHODIMP
nsViewSourceChannel::GetResponseStatus(uint32_t *aResponseStatus)
{
    return !mHttpChannel ? NS_ERROR_NULL_POINTER :
        mHttpChannel->GetResponseStatus(aResponseStatus);
}

NS_IMETHODIMP
nsViewSourceChannel::GetResponseStatusText(nsACString & aResponseStatusText)
{
    return !mHttpChannel ? NS_ERROR_NULL_POINTER :
        mHttpChannel->GetResponseStatusText(aResponseStatusText);
}

NS_IMETHODIMP
nsViewSourceChannel::GetRequestSucceeded(bool *aRequestSucceeded)
{
    return !mHttpChannel ? NS_ERROR_NULL_POINTER :
        mHttpChannel->GetRequestSucceeded(aRequestSucceeded);
}

NS_IMETHODIMP
nsViewSourceChannel::GetResponseHeader(const nsACString & aHeader,
                                       nsACString & aValue)
{
    aValue.Truncate();
    if (!mHttpChannel)
        return NS_ERROR_NULL_POINTER;

    if (!aHeader.Equals(NS_LITERAL_CSTRING("Content-Type"),
                        nsCaseInsensitiveCStringComparator()) &&
        !aHeader.Equals(NS_LITERAL_CSTRING("Content-Security-Policy"),
                        nsCaseInsensitiveCStringComparator()) &&
        !aHeader.Equals(NS_LITERAL_CSTRING("Content-Security-Policy-Report-Only"),
                        nsCaseInsensitiveCStringComparator()) &&
        !aHeader.Equals(NS_LITERAL_CSTRING("X-Frame-Options"),
                        nsCaseInsensitiveCStringComparator())) {
        return NS_OK;
    }

    return mHttpChannel->GetResponseHeader(aHeader, aValue);
}

NS_IMETHODIMP
nsViewSourceChannel::SetResponseHeader(const nsACString & header,
                                       const nsACString & value, bool merge)
{
    return !mHttpChannel ? NS_ERROR_NULL_POINTER :
        mHttpChannel->SetResponseHeader(header, value, merge);
}

NS_IMETHODIMP
nsViewSourceChannel::VisitResponseHeaders(nsIHttpHeaderVisitor *aVisitor)
{
    if (!mHttpChannel)
        return NS_ERROR_NULL_POINTER;

    NS_NAMED_LITERAL_CSTRING(contentTypeStr, "Content-Type");
    nsAutoCString contentType;
    nsresult rv =
        mHttpChannel->GetResponseHeader(contentTypeStr, contentType);
    if (NS_SUCCEEDED(rv))
        aVisitor->VisitHeader(contentTypeStr, contentType);
    return NS_OK;
}

NS_IMETHODIMP
nsViewSourceChannel::GetOriginalResponseHeader(const nsACString & aHeader,
                                               nsIHttpHeaderVisitor *aVisitor)
{
    nsAutoCString value;
    nsresult rv = GetResponseHeader(aHeader, value);
    if (NS_FAILED(rv)) {
        return rv;
    }
    aVisitor->VisitHeader(aHeader, value);
    return NS_OK;
}

NS_IMETHODIMP
nsViewSourceChannel::VisitOriginalResponseHeaders(nsIHttpHeaderVisitor *aVisitor)
{
    return VisitResponseHeaders(aVisitor);
}

NS_IMETHODIMP
nsViewSourceChannel::IsNoStoreResponse(bool *_retval)
{
    return !mHttpChannel ? NS_ERROR_NULL_POINTER :
        mHttpChannel->IsNoStoreResponse(_retval);
}

NS_IMETHODIMP
nsViewSourceChannel::IsNoCacheResponse(bool *_retval)
{
    return !mHttpChannel ? NS_ERROR_NULL_POINTER :
        mHttpChannel->IsNoCacheResponse(_retval);
}

NS_IMETHODIMP
nsViewSourceChannel::IsPrivateResponse(bool *_retval)
{
    return !mHttpChannel ? NS_ERROR_NULL_POINTER :
        mHttpChannel->IsPrivateResponse(_retval);
}

NS_IMETHODIMP
nsViewSourceChannel::RedirectTo(nsIURI *uri)
{
    return !mHttpChannel ? NS_ERROR_NULL_POINTER :
        mHttpChannel->RedirectTo(uri);
}

NS_IMETHODIMP
nsViewSourceChannel::GetRequestContextID(nsID *_retval)
{
    return !mHttpChannel ? NS_ERROR_NULL_POINTER :
        mHttpChannel->GetRequestContextID(_retval);
}

NS_IMETHODIMP
nsViewSourceChannel::SetRequestContextID(const nsID rcid)
{
    return !mHttpChannel ? NS_ERROR_NULL_POINTER :
        mHttpChannel->SetRequestContextID(rcid);
}

NS_IMETHODIMP
nsViewSourceChannel::GetIsMainDocumentChannel(bool* aValue)
{
    return !mHttpChannel ? NS_ERROR_NULL_POINTER :
        mHttpChannel->GetIsMainDocumentChannel(aValue);
}

NS_IMETHODIMP
nsViewSourceChannel::SetIsMainDocumentChannel(bool aValue)
{
    return !mHttpChannel ? NS_ERROR_NULL_POINTER :
        mHttpChannel->SetIsMainDocumentChannel(aValue);
}

// Have to manually forward SetCorsPreflightParameters since it's [notxpcom]
void
nsViewSourceChannel::SetCorsPreflightParameters(const nsTArray<nsCString>& aUnsafeHeaders)
{
  mHttpChannelInternal->SetCorsPreflightParameters(aUnsafeHeaders);
}
