/* BlueZ D-Bus peripheral. See bluez1.md for commands and pairing policy. */

/*
export DBUS_SYSTEM_BUS_ADDRESS=unix:path=/var/run/dbus/system_bus_socket
*/

#include "bluez1.h"
#include <gio/gio.h>
#include <string.h>
#include <stdio.h>

void *bluez1_global;
static const char *root = "/com/algae/bluez1";
static const char *svc = "/com/algae/bluez1/service0";
static const char *rx = "/com/algae/bluez1/service0/rx";
static const char *tx = "/com/algae/bluez1/service0/tx";
static const char *agent = "/com/algae/bluez1_agent";
static const char *advert = "/com/algae/bluez1_advert";
static const char *uuid = "12345678-1234-5678-1234-56789abcdef0";
static const char *chif = "org.bluez.GattCharacteristic1";
typedef struct State {
    GMainContext *context;
    GMainLoop *loop;
    GDBusConnection *bus;
    GDBusNodeInfo *xml;
    GThread *worker;
    guint objects[6];
    unsigned object_count;
    unsigned char value[20];
    size_t value_size;
    char *adapter;
    gboolean notifying, active, pairing;
    gboolean old_pairable, restore_pairable;
    GMutex mutex;
    GCond cond;
    gboolean ready;
    int start_result;
    bluez1_receive_fn receive;
    void *arg;
} State;
static const char introspection[] =
"<node>\n"
"<interface name=\"org.freedesktop.DBus.ObjectManager\"><method name=\"GetManagedObjects\"><arg type=\"a{oa{sa{sv}}}\" direction=\"out\"/></method></interface>\n"
"<interface name=\"org.bluez.GattService1\"><property name=\"UUID\" type=\"s\" access=\"read\"/><property name=\"Primary\" type=\"b\" access=\"read\"/></interface>\n"
"<interface name=\"org.bluez.GattCharacteristic1\">\n"
"<method name=\"ReadValue\"><arg type=\"a{sv}\" direction=\"in\"/><arg type=\"ay\" direction=\"out\"/></method>\n"
"<method name=\"WriteValue\"><arg type=\"ay\" direction=\"in\"/><arg type=\"a{sv}\" direction=\"in\"/></method>\n"
"<method name=\"StartNotify\"/><method name=\"StopNotify\"/>\n"
"<property name=\"UUID\" type=\"s\" access=\"read\"/><property name=\"Service\" type=\"o\" access=\"read\"/><property name=\"Flags\" type=\"as\" access=\"read\"/><property name=\"Value\" type=\"ay\" access=\"read\"/><property name=\"Notifying\" type=\"b\" access=\"read\"/>\n"
"</interface>\n"
"<interface name=\"org.bluez.LEAdvertisement1\"><method name=\"Release\"/><property name=\"Type\" type=\"s\" access=\"read\"/><property name=\"ServiceUUIDs\" type=\"as\" access=\"read\"/><property name=\"LocalName\" type=\"s\" access=\"read\"/></interface>\n"
"<interface name=\"org.bluez.Agent1\">\n"
"<method name=\"Release\"/><method name=\"Cancel\"/>\n"
"<method name=\"RequestAuthorization\"><arg type=\"o\" direction=\"in\"/></method>\n"
"<method name=\"AuthorizeService\"><arg type=\"o\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/></method>\n"
"<method name=\"RequestConfirmation\"><arg type=\"o\" direction=\"in\"/><arg type=\"u\" direction=\"in\"/></method>\n"
"<method name=\"RequestPinCode\"><arg type=\"o\" direction=\"in\"/><arg type=\"s\" direction=\"out\"/></method>\n"
"<method name=\"RequestPasskey\"><arg type=\"o\" direction=\"in\"/><arg type=\"u\" direction=\"out\"/></method>\n"
"<method name=\"DisplayPinCode\"><arg type=\"o\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/></method>\n"
"<method name=\"DisplayPasskey\"><arg type=\"o\" direction=\"in\"/><arg type=\"u\" direction=\"in\"/><arg type=\"q\" direction=\"in\"/></method>\n"
"</interface></node>\n";
static GVariant *bytes(State *s, size_t offset) {
    return g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,
        s->value + offset, s->value_size - offset, 1);
}
static GVariant *property(State *s, const char *path, const char *name) {
    gboolean isrx = !strcmp(path, rx);
    if (!strcmp(path, svc)) {
        if (!strcmp(name, "UUID")) return g_variant_new_string(uuid);
        if (!strcmp(name, "Primary")) return g_variant_new_boolean(TRUE);
    } else if (!strcmp(path, rx) || !strcmp(path, tx)) {
        if (!strcmp(name, "UUID")) return g_variant_new_string(isrx ?
            "12345678-1234-5678-1234-56789abcdef1" : "12345678-1234-5678-1234-56789abcdef2");
        if (!strcmp(name, "Service")) return g_variant_new_object_path(svc);
        if (!strcmp(name, "Flags")) {
            const char *rf[] = {"write", "write-without-response", "encrypt-write", NULL};
            const char *tf[] = {"read", "notify", "encrypt-read", NULL};
            return g_variant_new_strv(isrx ? rf : tf, -1);
        }
        if (!strcmp(name, "Value")) return bytes(s, 0);
        if (!strcmp(name, "Notifying")) return g_variant_new_boolean(!isrx && s->notifying);
    } else if (!strcmp(path, advert)) {
        if (!strcmp(name, "Type")) return g_variant_new_string("peripheral");
        if (!strcmp(name, "LocalName")) return g_variant_new_string("tester-bluez");
        if (!strcmp(name, "ServiceUUIDs")) return g_variant_new_strv(&uuid, 1);
    }
    return NULL;
}
static void changed(State *s, const char *name, GVariant *value) {
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&b, "{sv}", name, value);
    g_dbus_connection_emit_signal(s->bus, NULL, tx,
        "org.freedesktop.DBus.Properties", "PropertiesChanged",
        g_variant_new("(sa{sv}as)", chif, &b, NULL), NULL);
}
static void method(GDBusConnection *connection, const gchar *sender, const gchar *path,
        const gchar *iface, const gchar *name, GVariant *params,
        GDBusMethodInvocation *inv, gpointer user) {
    State *s = (State *)user;
    (void)connection; (void)sender;
    if (!strcmp(iface, "org.freedesktop.DBus.ObjectManager")) {
        GVariantBuilder objects;
        g_variant_builder_init(&objects, G_VARIANT_TYPE("a{oa{sa{sv}}}"));
        const char *paths[] = {svc, rx, tx};
        for (size_t j = 0; j < G_N_ELEMENTS(paths); ++j) {
            const char *p = paths[j];
            const char *in = !strcmp(p, svc) ? "org.bluez.GattService1" : chif;
            GDBusInterfaceInfo *info = g_dbus_node_info_lookup_interface(s->xml, in);
            GVariantBuilder props, interfaces;
            g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
            for (GDBusPropertyInfo **i = info->properties; *i; ++i)
                g_variant_builder_add(&props, "{sv}", (*i)->name, property(s, p, (*i)->name));
            g_variant_builder_init(&interfaces, G_VARIANT_TYPE("a{sa{sv}}"));
            g_variant_builder_add(&interfaces, "{sa{sv}}", in, &props);
            g_variant_builder_add(&objects, "{oa{sa{sv}}}", p, &interfaces);
        }
        g_dbus_method_invocation_return_value(inv, g_variant_new("(a{oa{sa{sv}}})", &objects));
        return;
    }
    if (!strcmp(iface, "org.bluez.Agent1")) {
        if (!strcmp(name, "RequestAuthorization")) {
            /* Explicit demo policy: accept Just Works while module is active. */
            fprintf(stdout, "bluez1: accepting Just Works pairing\n");
        } else if (!strcmp(name, "AuthorizeService")) {
            const char *device, *service;
            g_variant_get(params, "(&o&s)", &device, &service);
            if (strcmp(service, uuid)) {
                g_dbus_method_invocation_return_dbus_error(inv, "org.bluez.Error.Rejected", "Service not allowed"); return;
            }
        } else if (!strcmp(name, "Release") || !strcmp(name, "Cancel")) {
            fprintf(stdout, "bluez1: agent %s\n", name);
        } else {
            g_dbus_method_invocation_return_dbus_error(inv, "org.bluez.Error.Rejected", "NoInputNoOutput agent does not accept PIN/passkey methods"); return;
        }
    } else if (!strcmp(iface, chif)) {
        if (!strcmp(name, "WriteValue") && !strcmp(path, rx)) {
            GVariant *v, *options;
            g_variant_get(params, "(@ay@a{sv})", &v, &options);
            gsize size;
            const unsigned char *data = (const unsigned char *)g_variant_get_fixed_array(v, &size, 1);
            guint16 offset = 0;
            gboolean prepare = FALSE;
            g_variant_lookup(options, "offset", "q", &offset);
            g_variant_lookup(options, "prepare-authorize", "b", &prepare);
            if (offset || prepare || size > 20) {
                g_dbus_method_invocation_return_dbus_error(inv, "org.bluez.Error.NotSupported", "Only complete messages of at most 20 bytes are supported");
            } else {
                fprintf(stdout, "bluez1 RX (%zu bytes):", size);
                for (gsize i = 0; i < size; ++i) fprintf(stdout, " %02x", data[i]);
                fprintf(stdout, "\n");
                if (s->receive) s->receive(data, size, s->arg);
                g_dbus_method_invocation_return_value(inv, NULL);
            }
            g_variant_unref(v); g_variant_unref(options); return;
        } else if (!strcmp(name, "ReadValue") && !strcmp(path, tx)) {
            GVariant *options;
            guint16 offset = 0;
            g_variant_get(params, "(@a{sv})", &options);
            g_variant_lookup(options, "offset", "q", &offset);
            g_variant_unref(options);
            if (offset > s->value_size) g_dbus_method_invocation_return_dbus_error(inv, "org.bluez.Error.InvalidOffset", "Offset exceeds value length");
            else g_dbus_method_invocation_return_value(inv, g_variant_new("(@ay)", bytes(s, offset)));
            return;
        } else if ((!strcmp(name, "StartNotify") || !strcmp(name, "StopNotify")) && !strcmp(path, tx)) {
            s->notifying = !strcmp(name, "StartNotify");
            changed(s, "Notifying", g_variant_new_boolean(s->notifying));
        } else { g_dbus_method_invocation_return_dbus_error(inv, "org.bluez.Error.NotSupported", "Operation not supported by this characteristic"); return; }
    }
    g_dbus_method_invocation_return_value(inv, NULL);
}
static GVariant *getprop(GDBusConnection *connection, const gchar *sender, const gchar *path,
        const gchar *iface, const gchar *name, GError **error, gpointer user) {
    (void)connection; (void)sender; (void)iface; (void)error;
    return property((State *)user, path, name);
}
static const GDBusInterfaceVTable vtable = {method, getprop, NULL, {NULL}};
typedef struct Pending {
    gboolean done;
    GVariant *reply;
    GError *error;
} Pending;
static void call_done(GObject *obj, GAsyncResult *res, gpointer data) {
    Pending *p = (Pending *)data;
    p->reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(obj), res, &p->error);
    p->done = TRUE;
}
/* Dispatch while awaiting BlueZ: RegisterApplication calls our ObjectManager. */
static GVariant *call(State *s, const char *path, const char *iface,
        const char *name, GVariant *params) {
    Pending p;
    memset(&p, 0, sizeof(p));
    g_dbus_connection_call(s->bus, "org.bluez", path, iface, name, params,
        NULL, G_DBUS_CALL_FLAGS_NONE, 30000, NULL, call_done, &p);
    while (!p.done) g_main_context_iteration(s->context, TRUE);
    if (p.error) {
        fprintf(stderr, "bluez1 %s: %s\n", name, p.error->message);
        g_error_free(p.error);
    }
    return p.reply;
}
static gboolean command(State *s, const char *path, const char *iface, const char *name, GVariant *params) {
    GVariant *v = call(s, path, iface, name, params);
    if (!v) return FALSE;
    g_variant_unref(v); return TRUE;
}
static void cleanup(State *s) {
    if (s->bus) {
        if (s->restore_pairable) command(s, s->adapter, "org.freedesktop.DBus.Properties", "Set",
            g_variant_new("(ssv)", "org.bluez.Adapter1", "Pairable", g_variant_new_boolean(s->old_pairable)));
        /* A private bus connection owns all registrations. Closing it unregisters
         * advertisement, GATT application and agent, including partial startup. */
        for (unsigned i = 0; i < s->object_count; ++i)
            g_dbus_connection_unregister_object(s->bus, s->objects[i]);
        g_dbus_connection_close_sync(s->bus, NULL, NULL);
        g_object_unref(s->bus); s->bus = NULL;
    }
    s->object_count = 0;
    if (s->xml) { g_dbus_node_info_unref(s->xml); s->xml = NULL; }
    s->active = s->notifying = s->restore_pairable = FALSE;
}
static int setup(State *s) {
    GError *error = NULL;
    gchar *address = g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (address) {
        s->bus = g_dbus_connection_new_for_address_sync(address,
            (GDBusConnectionFlags)(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
            NULL, NULL, &error);
        g_free(address);
    }
    if (!s->bus) {
        fprintf(stderr, "bluez1 bus: %s\n", error ? error->message : "connection failed");
        g_clear_error(&error); return -1;
    }
    g_dbus_connection_set_exit_on_close(s->bus, FALSE);
    s->xml = g_dbus_node_info_new_for_xml(introspection, &error);
    if (!s->xml) { g_clear_error(&error); return -1; }
    const char *paths[] = {root, svc, rx, tx, advert, agent};
    const char *ifaces[] = {"org.freedesktop.DBus.ObjectManager", "org.bluez.GattService1", chif, chif,
        "org.bluez.LEAdvertisement1", "org.bluez.Agent1"};
    for (unsigned i = 0; i < 6; ++i) {
        guint id = g_dbus_connection_register_object(s->bus, paths[i],
            g_dbus_node_info_lookup_interface(s->xml, ifaces[i]), &vtable, s, NULL, &error);
        if (!id) { fprintf(stderr, "bluez1 export: %s\n", error->message); g_error_free(error); return -1; }
        s->objects[s->object_count++] = id;
    }
    GVariant *v = call(s, s->adapter, "org.freedesktop.DBus.Properties", "Get",
        g_variant_new("(ss)", "org.bluez.Adapter1", "Pairable"));
    if (!v) return -1;
    GVariant *boxed, *b;
    g_variant_get(v, "(@v)", &boxed); b = g_variant_get_variant(boxed);
    s->old_pairable = g_variant_get_boolean(b);
    g_variant_unref(b); g_variant_unref(boxed); g_variant_unref(v);
    if (!command(s, s->adapter, "org.freedesktop.DBus.Properties", "Set",
            g_variant_new("(ssv)", "org.bluez.Adapter1", "Powered", g_variant_new_boolean(TRUE)))) return -1;
    if (!command(s, "/org/bluez", "org.bluez.AgentManager1", "RegisterAgent", g_variant_new("(os)", agent, "NoInputNoOutput")) ||
        !command(s, "/org/bluez", "org.bluez.AgentManager1", "RequestDefaultAgent", g_variant_new("(o)", agent))) return -1;
    if (!command(s, s->adapter, "org.freedesktop.DBus.Properties", "Set",
            g_variant_new("(ssv)", "org.bluez.Adapter1", "Pairable", g_variant_new_boolean(TRUE)))) return -1;
    s->restore_pairable = TRUE;
    if (!command(s, s->adapter, "org.bluez.GattManager1", "RegisterApplication", g_variant_new("(oa{sv})", root, NULL)) ||
        !command(s, s->adapter, "org.bluez.LEAdvertisingManager1", "RegisterAdvertisement", g_variant_new("(oa{sv})", advert, NULL))) return -1;
    s->active = TRUE;
    return 0;
}
/* Public calls are serialized by the caller. The condition variable also
 * publishes startup and command results across the worker thread boundary. */
typedef int (*work_fn)(State *, void *);
typedef struct Work {
    State *state;
    work_fn fn;
    void *arg;
    GMutex mutex;
    GCond cond;
    gboolean done;
    int result;
} Work;
static gboolean run_work(gpointer data) {
    Work *w = (Work *)data;
    int result = w->fn(w->state, w->arg);
    g_mutex_lock(&w->mutex);
    w->result = result;
    w->done = TRUE;
    g_cond_signal(&w->cond);
    g_mutex_unlock(&w->mutex);
    return G_SOURCE_REMOVE;
}
static int dispatch(State *s, work_fn fn, void *arg) {
    Work w;
    memset(&w, 0, sizeof(w));
    w.state = s; w.fn = fn; w.arg = arg;
    g_mutex_init(&w.mutex); g_cond_init(&w.cond);
    g_mutex_lock(&w.mutex);
    /* Attach explicitly so work is always executed by the worker. */
    GSource *source = g_idle_source_new();
    g_source_set_callback(source, run_work, &w, NULL);
    g_source_attach(source, s->context);
    g_source_unref(source);
    while (!w.done) g_cond_wait(&w.cond, &w.mutex);
    g_mutex_unlock(&w.mutex);
    g_cond_clear(&w.cond); g_mutex_clear(&w.mutex);
    return w.result;
}
static gpointer worker_main(gpointer data) {
    State *s = (State *)data;
    g_main_context_push_thread_default(s->context);
    int rc = setup(s);
    g_mutex_lock(&s->mutex);
    s->start_result = rc; s->ready = TRUE;
    g_cond_signal(&s->cond);
    g_mutex_unlock(&s->mutex);
    if (!rc) g_main_loop_run(s->loop);
    cleanup(s);
    g_main_context_pop_thread_default(s->context);
    return NULL;
}
static void release_worker(State *s) {
    g_thread_join(s->worker); s->worker = NULL;
    g_main_loop_unref(s->loop); s->loop = NULL;
    g_main_context_unref(s->context); s->context = NULL;
    g_clear_pointer(&s->adapter, g_free);
}
void *bluez1_init(void *evctx) {
    (void)evctx;
    State *s = g_new0(State, 1);
    g_mutex_init(&s->mutex); g_cond_init(&s->cond);
    return s;
}
int bluez1_start(void *ctx, const char *adapter, bluez1_receive_fn receive, void *arg) {
    State *s = (State *)ctx;
    const char *path = adapter ? adapter : "/org/bluez/hci0";
    if (!s || s->worker || !g_variant_is_object_path(path)) return -1;
    s->adapter = g_strdup(path);
    s->receive = receive; s->arg = arg;
    s->context = g_main_context_new(); s->loop = g_main_loop_new(s->context, FALSE);
    s->ready = FALSE;
    GError *error = NULL;
    s->worker = g_thread_try_new("bluez1", worker_main, s, &error);
    if (!s->worker) {
        fprintf(stderr, "bluez1 thread: %s\n", error->message);
        g_error_free(error);
        g_main_loop_unref(s->loop); s->loop = NULL;
        g_main_context_unref(s->context); s->context = NULL;
        g_clear_pointer(&s->adapter, g_free);
        return -1;
    }
    g_mutex_lock(&s->mutex);
    while (!s->ready) g_cond_wait(&s->cond, &s->mutex);
    int rc = s->start_result;
    g_mutex_unlock(&s->mutex);
    if (rc) release_worker(s);
    return rc;
}
static int stop_work(State *s, void *arg) {
    (void)arg;
    g_main_loop_quit(s->loop);
    return 0;
}
int bluez1_stop(void *ctx) {
    State *s = (State *)ctx;
    if (!s) return -1;
    if (!s->worker) return 0;
    dispatch(s, stop_work, NULL);
    release_worker(s);
    return 0;
}
void bluez1_destroy(void *ctx) {
    State *s = (State *)ctx;
    if (!s) return;
    bluez1_stop(s);
    g_cond_clear(&s->cond); g_mutex_clear(&s->mutex);
    if (bluez1_global == ctx) bluez1_global = NULL;
    g_free(s);
}
typedef struct Message {
    const void *data;
    size_t size;
    int notify;
} Message;
static int send_work(State *s, void *arg) {
    const Message *m = (const Message *)arg;
    if (!s->active || g_dbus_connection_is_closed(s->bus) || (m->notify && !s->notifying)) return -1;
    if (m->size) memcpy(s->value, m->data, m->size);
    s->value_size = m->size;
    if (m->notify) changed(s, "Value", bytes(s, 0));
    return 0;
}
int bluez1_send(void *ctx, const void *data, size_t size, int notify) {
    State *s = (State *)ctx;
    if (!s || !s->worker || size > sizeof(s->value) || (size && !data)) return -1;
    Message m = {data, size, notify};
    return dispatch(s, send_work, &m);
}
static int pair_work(State *s, void *arg) {
    if (!s->active || s->pairing) return -1;
    s->pairing = TRUE;
    int rc = command(s, (const char *)arg, "org.bluez.Device1", "Pair", NULL) ? 0 : -1;
    s->pairing = FALSE;
    return rc;
}
int bluez1_pair(void *ctx, const char *device) {
    State *s = (State *)ctx;
    if (!s || !s->worker || !device || !g_variant_is_object_path(device)) return -1;
    /* dispatch waits for completion; caller owns the string for this duration. */
    return dispatch(s, pair_work, (void *)device);
}
int cli_cmd_bluez1(void *arg, int argc, const char **argv) {
    (void)arg;
    int rc = -1;
    if (argc >= 2 && bluez1_global) {
        if (!strcmp(argv[1], "start") && argc <= 3) rc = bluez1_start(bluez1_global, argc == 3 ? argv[2] : NULL, NULL, NULL);
        else if (!strcmp(argv[1], "stop") && argc == 2) rc = bluez1_stop(bluez1_global);
        else if (!strcmp(argv[1], "pair") && argc == 3) rc = bluez1_pair(bluez1_global, argv[2]);
        else if ((!strcmp(argv[1], "send") || !strcmp(argv[1], "notify")) && argc >= 3) {
            GString *message = g_string_new(NULL);
            for (int i = 2; i < argc; ++i) {
                if (i > 2) g_string_append_c(message, ' ');
                g_string_append(message, argv[i]);
            }
            rc = bluez1_send(bluez1_global, message->str, message->len, !strcmp(argv[1], "notify"));
            g_string_free(message, TRUE);
        }
    }
    if (rc) fprintf(stderr, "bluez1 failed. Usage: bluez1 start [adapter-path] | stop | pair <device-path> | send <text> | notify <text> (max 20 bytes; notify requires subscription)\n");
    return rc;
}
