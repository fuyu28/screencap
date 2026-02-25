#include "encode_wic_png.h"

#include <wincodec.h>

#include <wrl/client.h>

namespace sc {

bool SavePngWic(const ImageBuffer &img, const std::wstring &out_path,
                bool overwrite, ErrorInfo *err) {
  if (!overwrite) {
    DWORD attrs = GetFileAttributesW(out_path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
      *err = ErrorInfo{"output exists (use --overwrite)", "SavePngWic",
                       std::nullopt, std::nullopt};
      return false;
    }
  }

  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  bool need_uninit = SUCCEEDED(hr);
  if (hr == RPC_E_CHANGED_MODE) {
    need_uninit = false;
    hr = S_OK;
  }
  if (FAILED(hr)) {
    *err = ErrorInfo{"CoInitializeEx failed", "SavePngWic",
                     static_cast<uint32_t>(hr), std::nullopt};
    return false;
  }

  Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
  hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                        IID_PPV_ARGS(&factory));
  if (FAILED(hr)) {
    *err = ErrorInfo{"CoCreateInstance IWICImagingFactory failed", "SavePngWic",
                     static_cast<uint32_t>(hr), std::nullopt};
    if (need_uninit)
      CoUninitialize();
    return false;
  }

  Microsoft::WRL::ComPtr<IWICStream> stream;
  hr = factory->CreateStream(&stream);
  if (FAILED(hr)) {
    *err = ErrorInfo{"CreateStream failed", "SavePngWic",
                     static_cast<uint32_t>(hr), std::nullopt};
    if (need_uninit)
      CoUninitialize();
    return false;
  }

  hr = stream->InitializeFromFilename(out_path.c_str(), GENERIC_WRITE);
  if (FAILED(hr)) {
    *err = ErrorInfo{"InitializeFromFilename failed", "SavePngWic",
                     static_cast<uint32_t>(hr), std::nullopt};
    if (need_uninit)
      CoUninitialize();
    return false;
  }

  Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
  hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
  if (FAILED(hr)) {
    *err = ErrorInfo{"CreateEncoder failed", "SavePngWic",
                     static_cast<uint32_t>(hr), std::nullopt};
    if (need_uninit)
      CoUninitialize();
    return false;
  }

  hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
  if (FAILED(hr)) {
    *err = ErrorInfo{"Encoder Initialize failed", "SavePngWic",
                     static_cast<uint32_t>(hr), std::nullopt};
    if (need_uninit)
      CoUninitialize();
    return false;
  }

  Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
  Microsoft::WRL::ComPtr<IPropertyBag2> props;
  hr = encoder->CreateNewFrame(&frame, &props);
  if (FAILED(hr)) {
    *err = ErrorInfo{"CreateNewFrame failed", "SavePngWic",
                     static_cast<uint32_t>(hr), std::nullopt};
    if (need_uninit)
      CoUninitialize();
    return false;
  }

  hr = frame->Initialize(props.Get());
  if (FAILED(hr)) {
    *err = ErrorInfo{"Frame Initialize failed", "SavePngWic",
                     static_cast<uint32_t>(hr), std::nullopt};
    if (need_uninit)
      CoUninitialize();
    return false;
  }

  hr = frame->SetSize(static_cast<UINT>(img.width),
                      static_cast<UINT>(img.height));
  if (FAILED(hr)) {
    *err = ErrorInfo{"SetSize failed", "SavePngWic", static_cast<uint32_t>(hr),
                     std::nullopt};
    if (need_uninit)
      CoUninitialize();
    return false;
  }

  WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
  hr = frame->SetPixelFormat(&fmt);
  if (FAILED(hr)) {
    *err = ErrorInfo{"SetPixelFormat failed", "SavePngWic",
                     static_cast<uint32_t>(hr), std::nullopt};
    if (need_uninit)
      CoUninitialize();
    return false;
  }

  hr = frame->WritePixels(
      static_cast<UINT>(img.height), static_cast<UINT>(img.row_pitch),
      static_cast<UINT>(img.bgra.size()), const_cast<BYTE *>(img.bgra.data()));
  if (FAILED(hr)) {
    *err = ErrorInfo{"WritePixels failed", "SavePngWic",
                     static_cast<uint32_t>(hr), std::nullopt};
    if (need_uninit)
      CoUninitialize();
    return false;
  }

  hr = frame->Commit();
  if (FAILED(hr)) {
    *err = ErrorInfo{"Frame Commit failed", "SavePngWic",
                     static_cast<uint32_t>(hr), std::nullopt};
    if (need_uninit)
      CoUninitialize();
    return false;
  }

  hr = encoder->Commit();
  if (FAILED(hr)) {
    *err = ErrorInfo{"Encoder Commit failed", "SavePngWic",
                     static_cast<uint32_t>(hr), std::nullopt};
    if (need_uninit)
      CoUninitialize();
    return false;
  }

  if (need_uninit)
    CoUninitialize();
  return true;
}

} // namespace sc
