/* -*- Mode: C++; tab-width: 2; indent-tabs-mode:nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/PermissionMessageUtils.h"
#include "mozilla/Preferences.h"
#include "mozilla/Telemetry.h"
#include "nsXULAppAPI.h"

#include "nsAlertsService.h"

#ifdef MOZ_WIDGET_ANDROID
#include "AndroidBridge.h"
#else

#include "nsXPCOM.h"
#include "nsIServiceManager.h"
#include "nsIDOMWindow.h"
#include "nsPromiseFlatString.h"
#include "nsToolkitCompsCID.h"

#endif // !MOZ_WIDGET_ANDROID

#ifdef MOZ_PLACES
#include "mozIAsyncFavicons.h"
#include "nsIFaviconService.h"
#endif // MOZ_PLACES

using namespace mozilla;

using mozilla::dom::ContentChild;

namespace {

#ifdef MOZ_PLACES

class IconCallback final : public nsIFaviconDataCallback
{
public:
  NS_DECL_ISUPPORTS

  IconCallback(nsIAlertsService* aBackend,
               nsIAlertNotification* aAlert,
               nsIObserver* aAlertListener)
    : mBackend(aBackend)
    , mAlert(aAlert)
    , mAlertListener(aAlertListener)
  {}

  NS_IMETHOD
  OnComplete(nsIURI *aIconURI, uint32_t aIconSize, const uint8_t *aIconData,
             const nsACString &aMimeType) override
  {
    nsresult rv = NS_ERROR_FAILURE;
    if (aIconSize > 0) {
      nsCOMPtr<nsIAlertsIconData> alertsIconData(do_QueryInterface(mBackend));
      if (alertsIconData) {
        rv = alertsIconData->ShowAlertWithIconData(mAlert, mAlertListener,
                                                   aIconSize, aIconData);
      }
    } else if (aIconURI) {
      nsCOMPtr<nsIAlertsIconURI> alertsIconURI(do_QueryInterface(mBackend));
      if (alertsIconURI) {
        rv = alertsIconURI->ShowAlertWithIconURI(mAlert, mAlertListener,
                                                 aIconURI);
      }
    }
    if (NS_FAILED(rv)) {
      rv = mBackend->ShowAlert(mAlert, mAlertListener);
    }
    return rv;
  }

private:
  virtual ~IconCallback() {}

  nsCOMPtr<nsIAlertsService> mBackend;
  nsCOMPtr<nsIAlertNotification> mAlert;
  nsCOMPtr<nsIObserver> mAlertListener;
};

NS_IMPL_ISUPPORTS(IconCallback, nsIFaviconDataCallback)

#endif // MOZ_PLACES

#ifndef MOZ_WIDGET_ANDROID

nsresult
ShowWithIconBackend(nsIAlertsService* aBackend, nsIAlertNotification* aAlert,
                    nsIObserver* aAlertListener)
{
#ifdef MOZ_PLACES
  nsCOMPtr<nsIURI> uri;
  nsresult rv = aAlert->GetURI(getter_AddRefs(uri));
  if (NS_FAILED(rv) || !uri) {
    return NS_ERROR_FAILURE;
  }

  // Ensure the backend supports favicons.
  nsCOMPtr<nsIAlertsIconData> alertsIconData(do_QueryInterface(aBackend));
  nsCOMPtr<nsIAlertsIconURI> alertsIconURI;
  if (!alertsIconData) {
    alertsIconURI = do_QueryInterface(aBackend);
  }
  if (!alertsIconData && !alertsIconURI) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  nsCOMPtr<mozIAsyncFavicons> favicons(do_GetService(
    "@mozilla.org/browser/favicon-service;1"));
  NS_ENSURE_TRUE(favicons, NS_ERROR_FAILURE);

  nsCOMPtr<nsIFaviconDataCallback> callback =
    new IconCallback(aBackend, aAlert, aAlertListener);
  if (alertsIconData) {
    return favicons->GetFaviconDataForPage(uri, callback);
  }
  return favicons->GetFaviconURLForPage(uri, callback);
#else
  return NS_ERROR_NOT_IMPLEMENTED;
#endif // !MOZ_PLACES
}

nsresult
ShowWithBackend(nsIAlertsService* aBackend, nsIAlertNotification* aAlert,
                nsIObserver* aAlertListener)
{
  if (Preferences::GetBool("alerts.showFavicons")) {
    nsresult rv = ShowWithIconBackend(aBackend, aAlert, aAlertListener);
    if (NS_SUCCEEDED(rv)) {
      return rv;
    }
  }
  // If favicons are disabled, or the backend doesn't support them, show the
  // alert without one.
  return aBackend->ShowAlert(aAlert, aAlertListener);
}

#endif // MOZ_WIDGET_ANDROID

} // anonymous namespace

NS_IMPL_ISUPPORTS(nsAlertsService, nsIAlertsService, nsIAlertsDoNotDisturb, nsIAlertsProgressListener)

nsAlertsService::nsAlertsService() :
  mBackend(nullptr)
{
#ifndef MOZ_WIDGET_ANDROID
  mBackend = do_GetService(NS_SYSTEMALERTSERVICE_CONTRACTID);
#endif // MOZ_WIDGET_ANDROID
}

nsAlertsService::~nsAlertsService()
{}

bool nsAlertsService::ShouldShowAlert()
{
  bool result = true;

#ifdef XP_WIN
  HMODULE shellDLL = ::LoadLibraryW(L"shell32.dll");
  if (!shellDLL)
    return result;

  SHQueryUserNotificationStatePtr pSHQueryUserNotificationState =
    (SHQueryUserNotificationStatePtr) ::GetProcAddress(shellDLL, "SHQueryUserNotificationState");

  if (pSHQueryUserNotificationState) {
    MOZ_QUERY_USER_NOTIFICATION_STATE qstate;
    if (SUCCEEDED(pSHQueryUserNotificationState(&qstate))) {
      if (qstate != QUNS_ACCEPTS_NOTIFICATIONS) {
         result = false;
      }
    }
  }

  ::FreeLibrary(shellDLL);
#endif

  return result;
}

NS_IMETHODIMP nsAlertsService::ShowAlertNotification(const nsAString & aImageUrl, const nsAString & aAlertTitle,
                                                     const nsAString & aAlertText, bool aAlertTextClickable,
                                                     const nsAString & aAlertCookie,
                                                     nsIObserver * aAlertListener,
                                                     const nsAString & aAlertName,
                                                     const nsAString & aBidi,
                                                     const nsAString & aLang,
                                                     const nsAString & aData,
                                                     nsIPrincipal * aPrincipal,
                                                     bool aInPrivateBrowsing)
{
  nsCOMPtr<nsIAlertNotification> alert =
    do_CreateInstance(ALERT_NOTIFICATION_CONTRACTID);
  NS_ENSURE_TRUE(alert, NS_ERROR_FAILURE);
  nsresult rv = alert->Init(aAlertName, aImageUrl, aAlertTitle,
                            aAlertText, aAlertTextClickable,
                            aAlertCookie, aBidi, aLang, aData,
                            aPrincipal, aInPrivateBrowsing);
  NS_ENSURE_SUCCESS(rv, rv);
  return ShowAlert(alert, aAlertListener);
}


NS_IMETHODIMP nsAlertsService::ShowAlert(nsIAlertNotification * aAlert,
                                         nsIObserver * aAlertListener)
{
  NS_ENSURE_ARG(aAlert);

  nsAutoString cookie;
  nsresult rv = aAlert->GetCookie(cookie);
  NS_ENSURE_SUCCESS(rv, rv);

  if (XRE_IsContentProcess()) {
    ContentChild* cpc = ContentChild::GetSingleton();

    if (aAlertListener)
      cpc->AddRemoteAlertObserver(cookie, aAlertListener);

    cpc->SendShowAlert(aAlert);
    return NS_OK;
  }

#ifdef MOZ_WIDGET_ANDROID
  nsAutoString imageUrl;
  rv = aAlert->GetImageURL(imageUrl);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoString title;
  rv = aAlert->GetTitle(title);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoString text;
  rv = aAlert->GetText(text);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoString name;
  rv = aAlert->GetName(name);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIPrincipal> principal;
  rv = aAlert->GetPrincipal(getter_AddRefs(principal));
  NS_ENSURE_SUCCESS(rv, rv);

  mozilla::AndroidBridge::Bridge()->ShowAlertNotification(imageUrl, title, text, cookie,
                                                          aAlertListener, name, principal);
  return NS_OK;
#else
  // Check if there is an optional service that handles system-level notifications
  if (mBackend) {
    rv = ShowWithBackend(mBackend, aAlert, aAlertListener);
    if (NS_SUCCEEDED(rv)) {
      return rv;
    }
    // If the system backend failed to show the alert, clear the backend and
    // retry with XUL notifications. Future alerts will always use XUL.
    mBackend = nullptr;
  }

  if (!ShouldShowAlert()) {
    // Do not display the alert. Instead call alertfinished and get out.
    if (aAlertListener)
      aAlertListener->Observe(nullptr, "alertfinished", cookie.get());
    return NS_OK;
  }

  // Use XUL notifications as a fallback if above methods have failed.
  nsCOMPtr<nsIAlertsService> xulBackend(nsXULAlerts::GetInstance());
  NS_ENSURE_TRUE(xulBackend, NS_ERROR_FAILURE);
  return ShowWithBackend(xulBackend, aAlert, aAlertListener);
#endif // !MOZ_WIDGET_ANDROID
}

NS_IMETHODIMP nsAlertsService::CloseAlert(const nsAString& aAlertName,
                                          nsIPrincipal* aPrincipal)
{
  if (XRE_IsContentProcess()) {
    ContentChild* cpc = ContentChild::GetSingleton();
    cpc->SendCloseAlert(nsAutoString(aAlertName), IPC::Principal(aPrincipal));
    return NS_OK;
  }

#ifdef MOZ_WIDGET_ANDROID
  widget::GeckoAppShell::CloseNotification(aAlertName);
  return NS_OK;
#else

  nsresult rv;
  // Try the system notification service.
  if (mBackend) {
    rv = mBackend->CloseAlert(aAlertName, aPrincipal);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      // If the system backend failed to close the alert, fall back to XUL for
      // future alerts.
      mBackend = nullptr;
    }
  } else {
    nsCOMPtr<nsIAlertsService> xulBackend(nsXULAlerts::GetInstance());
    NS_ENSURE_TRUE(xulBackend, NS_ERROR_FAILURE);
    rv = xulBackend->CloseAlert(aAlertName, aPrincipal);
  }
  return rv;
#endif // !MOZ_WIDGET_ANDROID
}


// nsIAlertsDoNotDisturb
NS_IMETHODIMP nsAlertsService::GetManualDoNotDisturb(bool* aRetVal)
{
#ifdef MOZ_WIDGET_ANDROID
  return NS_ERROR_NOT_IMPLEMENTED;
#else
  nsCOMPtr<nsIAlertsDoNotDisturb> alertsDND(GetDNDBackend());
  NS_ENSURE_TRUE(alertsDND, NS_ERROR_NOT_IMPLEMENTED);
  return alertsDND->GetManualDoNotDisturb(aRetVal);
#endif
}

NS_IMETHODIMP nsAlertsService::SetManualDoNotDisturb(bool aDoNotDisturb)
{
#ifdef MOZ_WIDGET_ANDROID
  return NS_ERROR_NOT_IMPLEMENTED;
#else
  nsCOMPtr<nsIAlertsDoNotDisturb> alertsDND(GetDNDBackend());
  NS_ENSURE_TRUE(alertsDND, NS_ERROR_NOT_IMPLEMENTED);

  nsresult rv = alertsDND->SetManualDoNotDisturb(aDoNotDisturb);
  if (NS_SUCCEEDED(rv)) {
    Telemetry::Accumulate(Telemetry::ALERTS_SERVICE_DND_ENABLED, 1);
  }
  return rv;
#endif
}

NS_IMETHODIMP nsAlertsService::OnProgress(const nsAString & aAlertName,
                                          int64_t aProgress,
                                          int64_t aProgressMax,
                                          const nsAString & aAlertText)
{
#ifdef MOZ_WIDGET_ANDROID
  widget::GeckoAppShell::AlertsProgressListener_OnProgress(aAlertName,
                                                           aProgress, aProgressMax,
                                                           aAlertText);
  return NS_OK;
#else
  return NS_ERROR_NOT_IMPLEMENTED;
#endif // !MOZ_WIDGET_ANDROID
}

NS_IMETHODIMP nsAlertsService::OnCancel(const nsAString & aAlertName)
{
#ifdef MOZ_WIDGET_ANDROID
  widget::GeckoAppShell::CloseNotification(aAlertName);
  return NS_OK;
#else
  return NS_ERROR_NOT_IMPLEMENTED;
#endif // !MOZ_WIDGET_ANDROID
}

already_AddRefed<nsIAlertsDoNotDisturb>
nsAlertsService::GetDNDBackend()
{
  // Try the system notification service.
  nsCOMPtr<nsIAlertsService> backend = mBackend;
  if (!backend) {
    backend = nsXULAlerts::GetInstance();
  }

  nsCOMPtr<nsIAlertsDoNotDisturb> alertsDND(do_QueryInterface(backend));
  return alertsDND.forget();
}
