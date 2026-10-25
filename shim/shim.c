// Injection shim for the 2019 NIZ macOS firmware tool.
//
// The tool writes 3..27 byte HID reports to a device whose OUT endpoint takes
// fixed 64-byte packets, and it never reads the device's replies. The first
// short write leaves the endpoint mid-transfer, so every later write returns
// kIOReturnTimeout (0xE00002D6). Pad the reports, open non-exclusively, and
// consume replies on a runloop we own.
#include <IOKit/hid/IOHIDLib.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static FILE *lg(void) {
    static FILE *f;
    if (!f) f = fopen("/tmp/nizshim.log", "a");
    return f;
}
#define LOG(...) do { FILE *f_ = lg(); if (f_) { fprintf(f_, __VA_ARGS__); fflush(f_); } } while (0)

#define MAXD 8
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static CFRunLoopRef g_rl;
static IOHIDDeviceRef g_dev[MAXD];
static uint8_t g_buf[MAXD][128];
static int g_ndev;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;

static void reportCb(void *c, IOReturn r, void *s, IOHIDReportType t,
                     uint32_t id, uint8_t *rep, CFIndex len) {
    LOG("  IN id=%u len=%ld first=%02x\n", id, (long)len, len > 0 ? rep[0] : 0);
}

// Pull any queued reply directly, in case the callback is not delivered while
// the tool's own runloop is busy.
static void *drainer(void *a) {
    for (;;) {
        pthread_mutex_lock(&g_mu);
        int n = g_ndev;
        IOHIDDeviceRef ds[MAXD];
        for (int i = 0; i < n; i++) ds[i] = g_dev[i];
        pthread_mutex_unlock(&g_mu);
        for (int i = 0; i < n; i++) {
            uint8_t in[128]; CFIndex len = sizeof in;
            IOReturn g = IOHIDDeviceGetReport(ds[i], kIOHIDReportTypeInput, 0, in, &len);
            if (g == kIOReturnSuccess)
                LOG("  DRAINED len=%ld first=%02x\n", (long)len, len > 0 ? in[0] : 0);
        }
        usleep(20000);
    }
    return NULL;
}

static void *pump(void *a) {
    pthread_mutex_lock(&g_mu);
    g_rl = CFRunLoopGetCurrent();
    CFRetain(g_rl);
    pthread_cond_broadcast(&g_cv);
    pthread_mutex_unlock(&g_mu);
    for (;;) CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
    return NULL;
}

static void start_pump(void) {
    pthread_t th;
    pthread_create(&th, NULL, pump, NULL);
    pthread_detach(th);
    pthread_create(&th, NULL, drainer, NULL);
    pthread_detach(th);
    pthread_mutex_lock(&g_mu);
    while (!g_rl) pthread_cond_wait(&g_cv, &g_mu);
    pthread_mutex_unlock(&g_mu);
}

// Callback must be registered and scheduled before the device is opened, or the
// reply to the first report is already stuck in the queue.
static IOReturn my_devopen(IOHIDDeviceRef d, IOOptionBits o) {
    LOG("DEVOPEN opts=%u\n", (unsigned)o);
    pthread_once(&g_once, start_pump);
    pthread_mutex_lock(&g_mu);
    int slot = g_ndev;
    if (slot < MAXD) g_dev[g_ndev++] = (IOHIDDeviceRef)CFRetain(d);
    CFRunLoopRef rl = g_rl;
    pthread_mutex_unlock(&g_mu);
    if (slot < MAXD) {
        IOHIDDeviceRegisterInputReportCallback(d, g_buf[slot], sizeof g_buf[slot],
                                               reportCb, NULL);
        IOHIDDeviceScheduleWithRunLoop(d, rl, kCFRunLoopDefaultMode);
        LOG("REGISTERED slot %d\n", slot);
    }
    IOReturn s = IOHIDDeviceOpen(d, kIOHIDOptionsTypeNone);
    LOG("DEVOPEN -> 0x%x\n", (unsigned)s);
    return s;
}

static IOReturn my_mgr_open(IOHIDManagerRef m, IOOptionBits o) {
    LOG("MGROPEN opts=%u\n", (unsigned)o);
    IOReturn s = IOHIDManagerOpen(m, kIOHIDOptionsTypeNone);
    LOG("MGROPEN -> 0x%x\n", (unsigned)s);
    return s;
}

static IOReturn my_setReport(IOHIDDeviceRef d, IOHIDReportType t, CFIndex id,
                             const uint8_t *r, CFIndex len) {
    uint8_t buf[64] = {0};
    if (len < 64) {
        memcpy(buf, r, len);
        r = buf;
    }
    CFIndex n = len < 64 ? 64 : len;
    IOReturn s = IOHIDDeviceSetReport(d, t, id, r, n);
    LOG("SET rid=%ld len=%ld(pad %ld) -> 0x%x\n", (long)id, (long)len, (long)n, (unsigned)s);
    return s;
}

#define DYLD_INTERPOSE(_repl, _orig) \
    __attribute__((used)) static struct { const void *r; const void *o; } _interpose_##_orig \
    __attribute__((section("__DATA,__interpose"))) = { (const void *)(unsigned long)&_repl, (const void *)(unsigned long)&_orig };
DYLD_INTERPOSE(my_devopen, IOHIDDeviceOpen)
DYLD_INTERPOSE(my_mgr_open, IOHIDManagerOpen)
DYLD_INTERPOSE(my_setReport, IOHIDDeviceSetReport)
