#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace portage::media {

class PortageMfByteStream final : public IMFByteStream {
  public:
    static PortageMfByteStream *CreateFromFd(int fd, int64_t offset, int64_t length, bool writable);

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    // IMFByteStream
    HRESULT STDMETHODCALLTYPE GetCapabilities(DWORD *pdwCapabilities) override;
    HRESULT STDMETHODCALLTYPE GetLength(QWORD *pqwLength) override;
    HRESULT STDMETHODCALLTYPE SetLength(QWORD qwLength) override;
    HRESULT STDMETHODCALLTYPE GetCurrentPosition(QWORD *pqwPosition) override;
    HRESULT STDMETHODCALLTYPE SetCurrentPosition(QWORD qwPosition) override;
    HRESULT STDMETHODCALLTYPE IsEndOfStream(BOOL *pfEndOfStream) override;
    HRESULT STDMETHODCALLTYPE Read(BYTE *pb, ULONG cb, ULONG *pcbRead) override;
    HRESULT STDMETHODCALLTYPE BeginRead(BYTE *pb, ULONG cb, IMFAsyncCallback *pCallback,
                                        IUnknown *punkState) override;
    HRESULT STDMETHODCALLTYPE EndRead(IMFAsyncResult *pResult, ULONG *pcbRead) override;
    HRESULT STDMETHODCALLTYPE Write(const BYTE *pb, ULONG cb, ULONG *pcbWritten) override;
    HRESULT STDMETHODCALLTYPE BeginWrite(const BYTE *pb, ULONG cb, IMFAsyncCallback *pCallback,
                                         IUnknown *punkState) override;
    HRESULT STDMETHODCALLTYPE EndWrite(IMFAsyncResult *pResult, ULONG *pcbWritten) override;
    HRESULT STDMETHODCALLTYPE Seek(MFBYTESTREAM_SEEK_ORIGIN SeekOrigin, LONGLONG llSeekOffset,
                                   DWORD dwSeekFlags, QWORD *pqwCurrentPosition) override;
    HRESULT STDMETHODCALLTYPE Flush() override;
    HRESULT STDMETHODCALLTYPE Close() override;

  private:
    PortageMfByteStream(HANDLE win_handle, bool owns_handle, std::string synthetic_bytes,
                        bool is_memory_backed, uint64_t base_offset, int64_t bounded_length,
                        bool writable);
    ~PortageMfByteStream();

    [[nodiscard]] uint64_t QueryActualLengthLocked() const;

    std::atomic<ULONG> ref_count_{1};
    std::mutex mutex_{};
    HANDLE handle_{INVALID_HANDLE_VALUE};
    bool owns_handle_{false};
    std::string memory_data_{};
    bool is_memory_backed_{false};
    uint64_t base_offset_{0};
    int64_t bounded_length_{-1};
    uint64_t cursor_{0};
    bool writable_{false};
    bool closed_{false};
};

} // namespace portage::media
