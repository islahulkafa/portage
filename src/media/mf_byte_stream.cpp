#include "media/mf_byte_stream.hpp"
#include "syscall/syscall_dispatcher.hpp"

#include <mferror.h>

#include <algorithm>
#include <cstring>
#include <io.h>
#include <limits>
#include <utility>

namespace portage::media {

namespace {

class AsyncIoState final : public IUnknown {
  public:
    AsyncIoState(HRESULT status, ULONG bytes) : status_(status), bytes_(bytes) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override
    {
        if (!ppv) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown) {
            *ppv = static_cast<IUnknown *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return ref_count_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG next = ref_count_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (next == 0) {
            delete this;
        }
        return next;
    }

    [[nodiscard]] HRESULT Status() const noexcept { return status_; }
    [[nodiscard]] ULONG Bytes() const noexcept { return bytes_; }

  private:
    ~AsyncIoState() = default;

    std::atomic<ULONG> ref_count_{1};
    HRESULT status_{S_OK};
    ULONG bytes_{0};
};

} // namespace

PortageMfByteStream *PortageMfByteStream::CreateFromFd(int fd, int64_t offset, int64_t length,
                                                       bool writable)
{
    if (fd < 0 || offset < 0) {
        return nullptr;
    }

    HANDLE source_handle = INVALID_HANDLE_VALUE;
    std::string memory_bytes;
    bool memory_backed = false;

    if (auto *fd_table = portage::syscall::SyscallDispatcher::GetFdTable()) {
        auto desc = fd_table->Get(fd);
        if (desc) {
            if (!desc->synthetic_data.empty() && !writable) {
                memory_backed = true;
                size_t start = static_cast<size_t>(
                    std::min<uint64_t>(static_cast<uint64_t>(offset), desc->synthetic_data.size()));
                size_t avail = desc->synthetic_data.size() - start;
                size_t take = (length >= 0) ? std::min(avail, static_cast<size_t>(length)) : avail;
                memory_bytes.assign(desc->synthetic_data.data() + start, take);
            } else if (desc->handle != INVALID_HANDLE_VALUE && desc->handle != nullptr) {
                source_handle = desc->handle;
            }
        }
    }

    if (!memory_backed && source_handle == INVALID_HANDLE_VALUE) {
        intptr_t raw = _get_osfhandle(fd);
        if (raw != -1) {
            source_handle = reinterpret_cast<HANDLE>(raw);
        }
    }

    if (!memory_backed) {
        if (source_handle == INVALID_HANDLE_VALUE || source_handle == nullptr) {
            return nullptr;
        }
        HANDLE dup = INVALID_HANDLE_VALUE;
        if (!DuplicateHandle(GetCurrentProcess(), source_handle, GetCurrentProcess(), &dup, 0,
                             FALSE, DUPLICATE_SAME_ACCESS)) {
            return nullptr;
        }
        return new PortageMfByteStream(dup, true, {}, false, static_cast<uint64_t>(offset), length,
                                       writable);
    }

    return new PortageMfByteStream(INVALID_HANDLE_VALUE, false, std::move(memory_bytes), true, 0,
                                   static_cast<int64_t>(memory_bytes.size()), false);
}

PortageMfByteStream::PortageMfByteStream(HANDLE win_handle, bool owns_handle,
                                         std::string synthetic_bytes, bool is_memory_backed,
                                         uint64_t base_offset, int64_t bounded_length,
                                         bool writable)
    : handle_(win_handle), owns_handle_(owns_handle), memory_data_(std::move(synthetic_bytes)),
      is_memory_backed_(is_memory_backed), base_offset_(base_offset),
      bounded_length_(bounded_length), writable_(writable)
{
}

PortageMfByteStream::~PortageMfByteStream() { Close(); }

HRESULT STDMETHODCALLTYPE PortageMfByteStream::QueryInterface(REFIID riid, void **ppv)
{
    if (!ppv) {
        return E_POINTER;
    }
    if (riid == IID_IUnknown || riid == IID_IMFByteStream) {
        *ppv = static_cast<IMFByteStream *>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE PortageMfByteStream::AddRef()
{
    return ref_count_.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG STDMETHODCALLTYPE PortageMfByteStream::Release()
{
    ULONG next = ref_count_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (next == 0) {
        delete this;
    }
    return next;
}

uint64_t PortageMfByteStream::QueryActualLengthLocked() const
{
    if (is_memory_backed_) {
        return memory_data_.size();
    }
    if (handle_ == INVALID_HANDLE_VALUE) {
        return 0;
    }
    LARGE_INTEGER file_size{};
    if (!GetFileSizeEx(handle_, &file_size) || file_size.QuadPart <= 0) {
        return 0;
    }
    uint64_t total = static_cast<uint64_t>(file_size.QuadPart);
    if (base_offset_ >= total) {
        return 0;
    }
    uint64_t avail = total - base_offset_;
    if (bounded_length_ >= 0) {
        return std::min<uint64_t>(avail, static_cast<uint64_t>(bounded_length_));
    }
    return avail;
}

HRESULT STDMETHODCALLTYPE PortageMfByteStream::GetCapabilities(DWORD *pdwCapabilities)
{
    if (!pdwCapabilities) {
        return E_POINTER;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
        return MF_E_SHUTDOWN;
    }
    DWORD caps = MFBYTESTREAM_IS_READABLE | MFBYTESTREAM_IS_SEEKABLE;
    if (writable_) {
        caps |= MFBYTESTREAM_IS_WRITABLE;
    }
    *pdwCapabilities = caps;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE PortageMfByteStream::GetLength(QWORD *pqwLength)
{
    if (!pqwLength) {
        return E_POINTER;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
        return MF_E_SHUTDOWN;
    }
    *pqwLength = QueryActualLengthLocked();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE PortageMfByteStream::SetLength(QWORD qwLength)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
        return MF_E_SHUTDOWN;
    }
    if (!writable_ || is_memory_backed_) {
        return E_NOTIMPL;
    }
    LARGE_INTEGER target{};
    target.QuadPart = static_cast<LONGLONG>(base_offset_ + qwLength);
    if (!SetFilePointerEx(handle_, target, nullptr, FILE_BEGIN) || !SetEndOfFile(handle_)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE PortageMfByteStream::GetCurrentPosition(QWORD *pqwPosition)
{
    if (!pqwPosition) {
        return E_POINTER;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
        return MF_E_SHUTDOWN;
    }
    *pqwPosition = cursor_;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE PortageMfByteStream::SetCurrentPosition(QWORD qwPosition)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
        return MF_E_SHUTDOWN;
    }
    cursor_ = qwPosition;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE PortageMfByteStream::IsEndOfStream(BOOL *pfEndOfStream)
{
    if (!pfEndOfStream) {
        return E_POINTER;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
        return MF_E_SHUTDOWN;
    }
    *pfEndOfStream = (cursor_ >= QueryActualLengthLocked()) ? TRUE : FALSE;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE PortageMfByteStream::Read(BYTE *pb, ULONG cb, ULONG *pcbRead)
{
    if (!pb) {
        return E_POINTER;
    }
    if (pcbRead) {
        *pcbRead = 0;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
        return MF_E_SHUTDOWN;
    }
    if (cb == 0) {
        return S_OK;
    }

    uint64_t total_len = QueryActualLengthLocked();
    if (cursor_ >= total_len) {
        return S_OK;
    }

    ULONG to_read = static_cast<ULONG>(std::min<uint64_t>(cb, total_len - cursor_));

    if (is_memory_backed_) {
        std::memcpy(pb, memory_data_.data() + cursor_, to_read);
        cursor_ += to_read;
        if (pcbRead) {
            *pcbRead = to_read;
        }
        return S_OK;
    }

    uint64_t abs_pos = base_offset_ + cursor_;
    OVERLAPPED ov{};
    ov.Offset = static_cast<DWORD>(abs_pos & 0xFFFFFFFFULL);
    ov.OffsetHigh = static_cast<DWORD>((abs_pos >> 32) & 0xFFFFFFFFULL);

    DWORD got = 0;
    if (!ReadFile(handle_, pb, to_read, &got, &ov)) {
        DWORD err = GetLastError();
        if (err == ERROR_HANDLE_EOF) {
            got = 0;
        } else {
            return HRESULT_FROM_WIN32(err);
        }
    }
    cursor_ += got;
    if (pcbRead) {
        *pcbRead = got;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE PortageMfByteStream::BeginRead(BYTE *pb, ULONG cb,
                                                         IMFAsyncCallback *pCallback,
                                                         IUnknown *punkState)
{
    if (!pCallback) {
        return E_INVALIDARG;
    }
    ULONG bytes_read = 0;
    HRESULT hr = Read(pb, cb, &bytes_read);
    auto *state = new AsyncIoState(hr, bytes_read);
    IMFAsyncResult *result = nullptr;
    HRESULT create_hr = MFCreateAsyncResult(state, pCallback, punkState, &result);
    state->Release();
    if (FAILED(create_hr)) {
        return create_hr;
    }
    HRESULT inv_hr = MFInvokeCallback(result);
    result->Release();
    return SUCCEEDED(inv_hr) ? S_OK : inv_hr;
}

HRESULT STDMETHODCALLTYPE PortageMfByteStream::EndRead(IMFAsyncResult *pResult, ULONG *pcbRead)
{
    if (!pResult) {
        return E_INVALIDARG;
    }
    IUnknown *unk = nullptr;
    HRESULT hr = pResult->GetObject(&unk);
    if (FAILED(hr) || !unk) {
        return FAILED(hr) ? hr : E_UNEXPECTED;
    }
    auto *state = static_cast<AsyncIoState *>(unk);
    HRESULT io_hr = state->Status();
    if (pcbRead) {
        *pcbRead = state->Bytes();
    }
    unk->Release();
    return io_hr;
}

HRESULT STDMETHODCALLTYPE PortageMfByteStream::Write(const BYTE *pb, ULONG cb, ULONG *pcbWritten)
{
    if (!pb) {
        return E_POINTER;
    }
    if (pcbWritten) {
        *pcbWritten = 0;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
        return MF_E_SHUTDOWN;
    }
    if (!writable_ || is_memory_backed_) {
        return E_ACCESSDENIED;
    }
    if (cb == 0) {
        return S_OK;
    }

    uint64_t to_write = cb;
    if (bounded_length_ >= 0) {
        uint64_t cap = static_cast<uint64_t>(bounded_length_);
        if (cursor_ >= cap) {
            return HRESULT_FROM_WIN32(ERROR_DISK_FULL);
        }
        to_write = std::min<uint64_t>(to_write, cap - cursor_);
    }

    uint64_t abs_pos = base_offset_ + cursor_;
    OVERLAPPED ov{};
    ov.Offset = static_cast<DWORD>(abs_pos & 0xFFFFFFFFULL);
    ov.OffsetHigh = static_cast<DWORD>((abs_pos >> 32) & 0xFFFFFFFFULL);

    DWORD wrote = 0;
    if (!WriteFile(handle_, pb, static_cast<DWORD>(to_write), &wrote, &ov)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    cursor_ += wrote;
    if (pcbWritten) {
        *pcbWritten = wrote;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE PortageMfByteStream::BeginWrite(const BYTE *pb, ULONG cb,
                                                          IMFAsyncCallback *pCallback,
                                                          IUnknown *punkState)
{
    if (!pCallback) {
        return E_INVALIDARG;
    }
    ULONG bytes_written = 0;
    HRESULT hr = Write(pb, cb, &bytes_written);
    auto *state = new AsyncIoState(hr, bytes_written);
    IMFAsyncResult *result = nullptr;
    HRESULT create_hr = MFCreateAsyncResult(state, pCallback, punkState, &result);
    state->Release();
    if (FAILED(create_hr)) {
        return create_hr;
    }
    HRESULT inv_hr = MFInvokeCallback(result);
    result->Release();
    return SUCCEEDED(inv_hr) ? S_OK : inv_hr;
}

HRESULT STDMETHODCALLTYPE PortageMfByteStream::EndWrite(IMFAsyncResult *pResult, ULONG *pcbWritten)
{
    if (!pResult) {
        return E_INVALIDARG;
    }
    IUnknown *unk = nullptr;
    HRESULT hr = pResult->GetObject(&unk);
    if (FAILED(hr) || !unk) {
        return FAILED(hr) ? hr : E_UNEXPECTED;
    }
    auto *state = static_cast<AsyncIoState *>(unk);
    HRESULT io_hr = state->Status();
    if (pcbWritten) {
        *pcbWritten = state->Bytes();
    }
    unk->Release();
    return io_hr;
}

HRESULT STDMETHODCALLTYPE PortageMfByteStream::Seek(MFBYTESTREAM_SEEK_ORIGIN SeekOrigin,
                                                    LONGLONG llSeekOffset, DWORD dwSeekFlags,
                                                    QWORD *pqwCurrentPosition)
{
    (void)dwSeekFlags;
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
        return MF_E_SHUTDOWN;
    }

    int64_t base = 0;
    if (SeekOrigin == msoBegin) {
        base = 0;
    } else if (SeekOrigin == msoCurrent) {
        base = static_cast<int64_t>(cursor_);
    } else {
        return E_INVALIDARG;
    }

    int64_t next = base + llSeekOffset;
    if (next < 0) {
        return E_INVALIDARG;
    }
    cursor_ = static_cast<uint64_t>(next);
    if (pqwCurrentPosition) {
        *pqwCurrentPosition = cursor_;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE PortageMfByteStream::Flush()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
        return MF_E_SHUTDOWN;
    }
    if (writable_ && handle_ != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(handle_);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE PortageMfByteStream::Close()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
        return S_OK;
    }
    closed_ = true;
    if (handle_ != INVALID_HANDLE_VALUE) {
        if (writable_) {
            FlushFileBuffers(handle_);
        }
        if (owns_handle_) {
            CloseHandle(handle_);
        }
        handle_ = INVALID_HANDLE_VALUE;
    }
    return S_OK;
}

} // namespace portage::media
