/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#if !defined(PDMFactory_h_)
#define PDMFactory_h_

#include "PlatformDecoderModule.h"
#include "mozilla/StaticMutex.h"

class CDMProxy;

namespace mozilla {

class DecoderDoctorDiagnostics;
class PDMFactoryImpl;
template<class T> class StaticAutoPtr;

class PDMFactory final {
public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(PDMFactory)

  PDMFactory();

  // Factory method that creates the appropriate PlatformDecoderModule for
  // the platform we're running on. Caller is responsible for deleting this
  // instance. It's expected that there will be multiple
  // PlatformDecoderModules alive at the same time.
  // This is called on the decode task queue.
  already_AddRefed<MediaDataDecoder>
  CreateDecoder(const TrackInfo& aConfig,
                TaskQueue* aTaskQueue,
                MediaDataDecoderCallback* aCallback,
                DecoderDoctorDiagnostics* aDiagnostics,
                layers::LayersBackend aLayersBackend = layers::LayersBackend::LAYERS_NONE,
                layers::ImageContainer* aImageContainer = nullptr) const;

  bool SupportsMimeType(const nsACString& aMimeType,
                        DecoderDoctorDiagnostics* aDiagnostics) const;

#ifdef MOZ_EME
  // Creates a PlatformDecoderModule that uses a CDMProxy to decrypt or
  // decrypt-and-decode EME encrypted content. If the CDM only decrypts and
  // does not decode, we create a PDM and use that to create MediaDataDecoders
  // that we use on on aTaskQueue to decode the decrypted stream.
  // This is called on the decode task queue.
  void SetCDMProxy(CDMProxy* aProxy);
#endif

private:
  virtual ~PDMFactory();
  void CreatePDMs();
  // Startup the provided PDM and add it to our list if successful.
  bool StartupPDM(PlatformDecoderModule* aPDM);
  // Returns the first PDM in our list supporting the mimetype.
  already_AddRefed<PlatformDecoderModule>
  GetDecoder(const nsACString& aMimeType,
             DecoderDoctorDiagnostics* aDiagnostics) const;

  already_AddRefed<MediaDataDecoder>
  CreateDecoderWithPDM(PlatformDecoderModule* aPDM,
                       const TrackInfo& aConfig,
                       TaskQueue* aTaskQueue,
                       MediaDataDecoderCallback* aCallback,
                       DecoderDoctorDiagnostics* aDiagnostics,
                       layers::LayersBackend aLayersBackend,
                       layers::ImageContainer* aImageContainer) const;

  nsTArray<RefPtr<PlatformDecoderModule>> mCurrentPDMs;
  RefPtr<PlatformDecoderModule> mEMEPDM;

  bool mWMFFailedToLoad = false;
  bool mFFmpegFailedToLoad = false;
  bool mGMPPDMFailedToStartup = false;

  void EnsureInit() const;
  template<class T> friend class StaticAutoPtr;
  static StaticAutoPtr<PDMFactoryImpl> sInstance;
  static StaticMutex sMonitor;
};

} // namespace mozilla

#endif /* PDMFactory_h_ */
