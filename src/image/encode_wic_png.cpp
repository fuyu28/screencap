#include "image/encode_wic_png.h"

#include <wincodec.h>

#include <wrl/client.h>

namespace sc {

bool SavePngWic(const ImageBuffer &img, const std::wstring &out_path,
                bool overwrite, ErrorInfo *err) {
  if (!overwrite) {
    DWORD attrs = GetFileAttributesW(out_path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
      return Fail(err, "output exists (use --overwrite)", "SavePngWic");
    }
  }

  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  bool need_uninit = SUCCEEDED(hr);
  if (hr == RPC_E_CHANGED_MODE) {
    need_uninit = false;
    hr = S_OK;
  }
  if (FAILED(hr)) {
    return FailHr(err, "CoInitializeEx failed", "SavePngWic", hr);
  }

  struct CoInitGuard {
    bool active = false;
    ~CoInitGuard() {
      if (active)
        CoUninitialize();
    }
  } co_guard{need_uninit};

  Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
  hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                        IID_PPV_ARGS(&factory));
  if (FAILED(hr)) {
    return FailHr(err, "CoCreateInstance IWICImagingFactory failed",
                  "SavePngWic", hr);
  }

  Microsoft::WRL::ComPtr<IWICStream> stream;
  hr = factory->CreateStream(&stream);
  if (FAILED(hr)) {
    return FailHr(err, "CreateStream failed", "SavePngWic", hr);
  }

  hr = stream->InitializeFromFilename(out_path.c_str(), GENERIC_WRITE);
  if (FAILED(hr)) {
    return FailHr(err, "InitializeFromFilename failed", "SavePngWic", hr);
  }

  Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
  hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
  if (FAILED(hr)) {
    return FailHr(err, "CreateEncoder failed", "SavePngWic", hr);
  }

  hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
  if (FAILED(hr)) {
    return FailHr(err, "Encoder Initialize failed", "SavePngWic", hr);
  }

  Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
  Microsoft::WRL::ComPtr<IPropertyBag2> props;
  hr = encoder->CreateNewFrame(&frame, &props);
  if (FAILED(hr)) {
    return FailHr(err, "CreateNewFrame failed", "SavePngWic", hr);
  }

  hr = frame->Initialize(props.Get());
  if (FAILED(hr)) {
    return FailHr(err, "Frame Initialize failed", "SavePngWic", hr);
  }

  hr = frame->SetSize(static_cast<UINT>(img.width),
                      static_cast<UINT>(img.height));
  if (FAILED(hr)) {
    return FailHr(err, "SetSize failed", "SavePngWic", hr);
  }

  WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
  hr = frame->SetPixelFormat(&fmt);
  if (FAILED(hr)) {
    return FailHr(err, "SetPixelFormat failed", "SavePngWic", hr);
  }

  hr = frame->WritePixels(
      static_cast<UINT>(img.height), static_cast<UINT>(img.row_pitch),
      static_cast<UINT>(img.bgra.size()), const_cast<BYTE *>(img.bgra.data()));
  if (FAILED(hr)) {
    return FailHr(err, "WritePixels failed", "SavePngWic", hr);
  }

  hr = frame->Commit();
  if (FAILED(hr)) {
    return FailHr(err, "Frame Commit failed", "SavePngWic", hr);
  }

  hr = encoder->Commit();
  if (FAILED(hr)) {
    return FailHr(err, "Encoder Commit failed", "SavePngWic", hr);
  }

  return true;
}

} // namespace sc
