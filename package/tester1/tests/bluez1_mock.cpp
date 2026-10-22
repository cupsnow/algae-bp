#include "../bluez1.h"
#include <gio/gio.h>
#include <thread>
#include <future>
#include <string>
#include <cstring>
#include <cstdio>
static const char *root = "/com/algae/bluez1";
static const char *agent = "/com/algae/bluez1_agent";
static const char *rx = "/com/algae/bluez1/service0/rx";
static const char *tx = "/com/algae/bluez1/service0/tx";
static const char *chif = "org.bluez.GattCharacteristic1";
#include <cassert>
static GDBusConnection *mock;
static GMainLoop *mockloop;
static const char mockxml[] = R"(<node>
<interface name='org.freedesktop.DBus.Properties'><method name='Get'><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='v' direction='out'/></method><method name='Set'><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='v' direction='in'/></method></interface>
<interface name='org.bluez.AgentManager1'><method name='RegisterAgent'><arg type='o' direction='in'/><arg type='s' direction='in'/></method><method name='RequestDefaultAgent'><arg type='o' direction='in'/></method></interface>
<interface name='org.bluez.GattManager1'><method name='RegisterApplication'><arg type='o' direction='in'/><arg type='a{sv}' direction='in'/></method></interface>
<interface name='org.bluez.LEAdvertisingManager1'><method name='RegisterAdvertisement'><arg type='o' direction='in'/><arg type='a{sv}' direction='in'/></method></interface>
<interface name='org.bluez.Device1'><method name='Pair'/></interface>
</node>)";
static std::string owner;
static void mockmethod(GDBusConnection *bus, const gchar *sender, const gchar *, const gchar *, const gchar *name, GVariant *, GDBusMethodInvocation *inv, gpointer) {
    if (!strcmp(name, "Pair")) {
        GError *err = nullptr;
        auto *v = g_dbus_connection_call_sync(bus, sender, agent, "org.bluez.Agent1",
            "RequestAuthorization", g_variant_new("(o)", "/org/bluez/hci0"), nullptr,
            G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, &err);
        assert(v && !err); g_variant_unref(v);
    }
    if (!strcmp(name, "Get")) { g_dbus_method_invocation_return_value(inv, g_variant_new("(v)", g_variant_new_boolean(FALSE))); return; }
    if (!strcmp(name, "RegisterApplication")) {
        owner = sender;
        GError *err = nullptr;
        GVariant *v = g_dbus_connection_call_sync(bus, sender, root, "org.freedesktop.DBus.ObjectManager", "GetManagedObjects", nullptr, G_VARIANT_TYPE("(a{oa{sa{sv}}})"), G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, &err);
        if (err) fprintf(stderr, "%s\n", err->message);
        assert(v); GVariant *objects = g_variant_get_child_value(v, 0); assert(g_variant_n_children(objects) == 3); g_variant_unref(objects); g_variant_unref(v);
    }
    g_dbus_method_invocation_return_value(inv, nullptr);
}
static const GDBusInterfaceVTable mockvt = {mockmethod, nullptr, nullptr, {nullptr}};
static GVariant *peer(const char *path, const char *method, GVariant *params) {
    GError *err = nullptr;
    auto *r = g_dbus_connection_call_sync(mock, owner.c_str(), path, chif, method, params, nullptr, G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, &err);
    if (err) g_error_free(err);
    return r;
}
static int received;
static void receive(const unsigned char *p, size_t n, void *) { assert(n == 2 && p[0] == 0x48 && p[1] == 0x69); ++received; }
int main() {
    std::promise<void> ready; auto future = ready.get_future();
    std::thread server([&] {
        auto *ctx = g_main_context_new(); g_main_context_push_thread_default(ctx);
        mockloop = g_main_loop_new(ctx, FALSE);
        mock = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr); assert(mock);
        auto *r = g_dbus_connection_call_sync(mock, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName", g_variant_new("(su)", "org.bluez", 0), nullptr, G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, nullptr); assert(r); g_variant_unref(r);
        auto *xml = g_dbus_node_info_new_for_xml(mockxml, nullptr);
        for (unsigned i = 0; xml->interfaces[i]; ++i) assert(g_dbus_connection_register_object(mock, i == 1 ? "/org/bluez" : "/org/bluez/hci0", xml->interfaces[i], &mockvt, nullptr, nullptr, nullptr));
        ready.set_value(); g_main_loop_run(mockloop);
        g_object_unref(mock); g_dbus_node_info_unref(xml); g_main_loop_unref(mockloop);
        g_main_context_pop_thread_default(ctx); g_main_context_unref(ctx);
    });
    future.get();
    auto *s = bluez1_init(nullptr);
    for (int i = 0; i < 2; ++i) {
        assert(bluez1_start(s, nullptr, receive, nullptr) == 0);
        assert(bluez1_start(s, nullptr, receive, nullptr) == -1);
        assert(bluez1_pair(s, "/org/bluez/hci0") == 0);
        assert(bluez1_send(s, "Hi", 2, 1) == -1);
        auto *v = peer(tx, "StartNotify", nullptr); assert(v); g_variant_unref(v);
        assert(bluez1_send(s, "Hi", 2, 1) == 0);
        v = peer(tx, "ReadValue", g_variant_new("(a{sv})", nullptr)); assert(v);
        auto *b = g_variant_get_child_value(v, 0); gsize n; const char *p = static_cast<const char *>(g_variant_get_fixed_array(b, &n, 1)); assert(n == 2 && !memcmp(p, "Hi", 2)); g_variant_unref(b); g_variant_unref(v);
        unsigned char data[] = {0x48, 0x69};
        v = peer(rx, "WriteValue", g_variant_new("(@aya{sv})", g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, data, 2, 1), nullptr)); assert(v); g_variant_unref(v);
        v = peer(rx, "ReadValue", g_variant_new("(a{sv})", nullptr)); assert(!v);
        assert(bluez1_send(s, "123456789012345678901", 21, 0) == -1);
        v = peer(tx, "StopNotify", nullptr); assert(v); g_variant_unref(v);
        assert(bluez1_stop(s) == 0); assert(bluez1_stop(s) == 0);
    }
    assert(received == 2); bluez1_destroy(s);
    g_main_loop_quit(mockloop); server.join(); puts("bluez1 mock lifecycle/GATT tests passed");
}
