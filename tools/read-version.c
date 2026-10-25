// Sends the version query to the NIZ vendor HID interface and prints the reply,
// e.g. mini84EC(S);V2.2.1;V1.0;
#include <IOKit/hid/IOHIDLib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static IOHIDDeviceRef found = NULL;
static int got = 0;

static void matchCb(void *c, IOReturn r, void *s, IOHIDDeviceRef d) {
    int page = 0;
    CFTypeRef p = IOHIDDeviceGetProperty(d, CFSTR(kIOHIDPrimaryUsagePageKey));
    if (p) CFNumberGetValue(p, kCFNumberIntType, &page);
    if (page == 140) found = (IOHIDDeviceRef)CFRetain(d);
}
static void reportCb(void *c, IOReturn r, void *s, IOHIDReportType t,
                     uint32_t id, uint8_t *rep, CFIndex len) {
    if (!got) {
        printf("%s\n", (char *)rep + 2);
        got = 1;
    }
}

int main(void) {
    IOHIDManagerRef m = IOHIDManagerCreate(kCFAllocatorDefault, 0);
    int vid = 0x483, pid = 0x5235;
    CFMutableDictionaryRef dict = CFDictionaryCreateMutable(NULL, 2, NULL, NULL);
    CFNumberRef v = CFNumberCreate(NULL, kCFNumberIntType, &vid);
    CFNumberRef p = CFNumberCreate(NULL, kCFNumberIntType, &pid);
    CFDictionarySetValue(dict, CFSTR(kIOHIDVendorIDKey), v);
    CFDictionarySetValue(dict, CFSTR(kIOHIDProductIDKey), p);
    IOHIDManagerSetDeviceMatching(m, dict);
    IOHIDManagerRegisterDeviceMatchingCallback(m, matchCb, NULL);
    IOHIDManagerScheduleWithRunLoop(m, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    IOHIDManagerOpen(m, 0);
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, false);
    if (!found) {
        fprintf(stderr, "keyboard not found (VID 0x0483 PID 0x5235)\n");
        return 1;
    }

    static uint8_t buf[128];
    IOHIDDeviceRegisterInputReportCallback(found, buf, sizeof buf, reportCb, NULL);
    IOHIDDeviceScheduleWithRunLoop(found, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    if (IOHIDDeviceOpen(found, 0) != kIOReturnSuccess) {
        fprintf(stderr, "cannot open vendor interface\n");
        return 1;
    }

    uint8_t out[64] = {0};
    out[1] = 0xf9;
    IOReturn s = IOHIDDeviceSetReport(found, kIOHIDReportTypeOutput, 0, out, 64);
    if (s != kIOReturnSuccess) {
        fprintf(stderr, "write failed: 0x%x (unplug and replug the keyboard)\n", (unsigned)s);
        return 1;
    }
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 2.0, false);

    IOHIDDeviceUnscheduleFromRunLoop(found, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    IOHIDDeviceClose(found, 0);
    IOHIDManagerClose(m, 0);
    CFRelease(found);
    if (!got) {
        fprintf(stderr, "no reply\n");
        return 1;
    }
    return 0;
}
