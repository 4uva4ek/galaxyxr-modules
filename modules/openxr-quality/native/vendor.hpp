// Client-side adapter for the stock Binder SHELL_COMMAND transaction.
// Tests null optional callbacks supported by the actual NDK handler.
// No remote memory access or patch; all opaque objects belong to this process.
#include <android/binder_ibinder.h>
#include <android/binder_parcel.h>
#include <android/binder_status.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static void need(bool ok, const char* msg) { if (!ok) { std::fprintf(stderr, "ERROR %s\n", msg); std::exit(1); } }
template<typename T> static T sym(void* lib, const char* name) {
    auto f = reinterpret_cast<T>(dlsym(lib, name)); need(f != nullptr, name); return f;
}
static void* create(void*) { return nullptr; }
static void destroy(void*) {}
static binder_status_t no_transaction(AIBinder*, transaction_code_t, const AParcel*, AParcel*) { return STATUS_UNKNOWN_TRANSACTION; }
// android::sp<T> is a pointer with a nontrivial destructor, returned indirectly
// through x8 on ARM64. This one-shot process intentionally holds the reference
// until process exit, avoiding any remote or guessed RefBase destruction.
struct PlatformRef { void* ptr = nullptr; ~PlatformRef() {} };
static int vendor_main(int argc, char** argv) {
    need((argc == 2 || argc == 3) && (!std::strcmp(argv[1], "help") || !std::strcmp(argv[1], "force-quality") ||
         !std::strcmp(argv[1], "normal-quality") || !std::strcmp(argv[1], "normal-efficiency")), "help | force-quality | normal-quality | normal-efficiency");
    FILE* hashes = popen("/system/bin/sha256sum /system/lib64/libbinder.so /system/lib64/libbinder_ndk.so /vendor/bin/hw/android.hardware.perceptionhost", "r");
    need(hashes, "Cannot verify command ABI");
    std::string identity; char line[1024];
    while (fgets(line, sizeof(line), hashes)) identity += line;
    need(pclose(hashes) == 0, "ABI hash command failed");
    for (const char* hash : {"1da138baee18d16352938d7d33598ed715be8ba31d2bdea23940537001c9f24c",
                            "43392c7b1e812d52578a8bdffe03a08420c877fde922bdfcfdbe537afde72bf2",
                            "562d027ff604a428b4004882589910be5a21dc1ee7972556929784abd8f44ac5"})
        need(identity.find(hash) != std::string::npos, "Unsupported command ABI");
    void* ndk = dlopen("libbinder_ndk.so", RTLD_NOW);
    void* cpp = dlopen("libbinder.so", RTLD_NOW);
    need(ndk && cpp, "Binder libraries unavailable");
    auto lookup = sym<AIBinder*(*)(const char*)>(ndk, "AServiceManager_checkService");
    AIBinder* binder = lookup("com.google.xr.hardware.perception.IPerceptionHost/default");
    need(binder && AIBinder_isRemote(binder), "Remote perception host unavailable");
    auto cls = AIBinder_Class_define("com.google.xr.hardware.perception.IPerceptionHost", create, destroy, no_transaction);
    need(cls && AIBinder_associateClass(binder, cls), "Unexpected Binder descriptor");
    AParcel* in = nullptr; AParcel* out = nullptr;
    need(AIBinder_prepareTransaction(binder, &in) == STATUS_OK, "Identity prepare failed");
    need(AIBinder_transact(binder, 16777215, &in, &out, 0) == STATUS_OK && out, "Identity transact failed");
    AStatus* s = nullptr; int32_t version = 0;
    need(AParcel_readStatusHeader(out, &s) == STATUS_OK && s && AStatus_isOk(s), "Identity status failed");
    need(AParcel_readInt32(out, &version) == STATUS_OK && version == 1, "Unexpected interface version");
    AStatus_delete(s); AParcel_delete(out);
    // Verify after acquiring this exact Binder handle. If the host restarts
    // afterward, this handle dies; it cannot silently address its replacement.
    if (std::strcmp(argv[1], "help"))
        need(argc == 3 && host_identity() == argv[2], "Perception process changed before command; nothing sent");

    auto to_platform = sym<PlatformRef(*)(AIBinder*)>(ndk, "_Z25AIBinder_toPlatformBinderP8AIBinder");
    PlatformRef ref = to_platform(binder); need(ref.ptr, "Platform Binder unavailable");
    auto ctor = sym<void(*)(void*)>(cpp, "_ZN7android6ParcelC1Ev");
    auto dtor = sym<void(*)(void*)>(cpp, "_ZN7android6ParcelD1Ev");
    auto fd_write = sym<int(*)(void*, int)>(cpp, "_ZN7android6Parcel22writeDupFileDescriptorEi");
    auto int_write = sym<int(*)(void*, int32_t)>(cpp, "_ZN7android6Parcel10writeInt32Ei");
    auto str_write = sym<int(*)(void*, const char16_t*, size_t)>(cpp, "_ZN7android6Parcel13writeString16EPKDsm");
    auto binder_write = sym<int(*)(void*, const PlatformRef&)>(cpp, "_ZN7android6Parcel17writeStrongBinderERKNS_2spINS_7IBinderEEE");
    auto transact = sym<int(*)(void*, uint32_t, const void*, void*, uint32_t)>(cpp, "_ZN7android8BpBinder8transactEjRKNS_6ParcelEPS1_j");
    // Pinned platform Parcel constructor uses far less than 4096 bytes. The
    // deliberately oversized aligned storage contains only client-owned data.
    alignas(16) unsigned char request[4096]{}, reply[4096]{};
    ctor(request); ctor(reply);
    int nullfd = open("/dev/null", O_RDWR | O_CLOEXEC);
    int pipefd[2]; need(pipe2(pipefd, O_CLOEXEC | O_NONBLOCK) == 0, "Output pipe failed");
    int output = pipefd[1];
    need(nullfd >= 0 && output >= 0, "Command descriptors unavailable");
    need(fd_write(request, nullfd) == 0 && fd_write(request, output) == 0 && fd_write(request, output) == 0,
         "Command descriptor parcel failed");
    std::vector<std::string> args;
    if (!std::strcmp(argv[1], "help")) args = {"help"};
    else args = {!std::strcmp(argv[1], "force-quality") ? "force-set-mode" : "set-mode", "1",
                 !std::strcmp(argv[1], "normal-efficiency") ? "2" : "1"};
    need(int_write(request, static_cast<int32_t>(args.size())) == 0, "Argument count failed");
    for (const auto& arg : args) {
        std::u16string utf16(arg.begin(), arg.end());
        need(str_write(request, utf16.data(), utf16.size()) == 0, "Argument parcel failed");
    }
    PlatformRef none;
    need(binder_write(request, none) == 0 && binder_write(request, none) == 0, "Null callback parcel failed");
    int result = transact(ref.ptr, 0x5f434d44, request, reply, 0);
    dtor(reply); dtor(request); close(nullfd);
    close(output); output = pipefd[0];
    char buf[4096]; ssize_t n;
    while ((n = read(output, buf, sizeof(buf))) > 0) std::fwrite(buf, 1, n, stdout);
    close(output); AIBinder_decStrong(binder);
    if (result) std::fprintf(stderr, "SHELL_TRANSACTION status=%d\n", result);
    return result ? 1 : 0;
}
