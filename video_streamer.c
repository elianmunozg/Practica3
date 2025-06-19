#include <gst/gst.h>
#include <glib.h>

static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
    GMainLoop *loop = (GMainLoop *)data;

    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            g_print("End of stream\n");
            g_main_loop_quit(loop);
            break;
        case GST_MESSAGE_ERROR: {
            gchar *debug;
            GError *error;
            gst_message_parse_error(msg, &error, &debug);
            g_free(debug);
            g_printerr("Error: %s\n", error->message);
            g_error_free(error);
            g_main_loop_quit(loop);
            break;
        }
        default:
            break;
    }
    return TRUE;
}

int main(int argc, char *argv[]) {
    GMainLoop *loop;
    GstElement *pipeline, *source, *conv, *filter, *enc, *pay, *sink;
    GstBus *bus;
    guint bus_watch_id;

    gst_init(&argc, &argv);
    loop = g_main_loop_new(NULL, FALSE);

    // Crear elementos
    pipeline = gst_pipeline_new("video-streamer");
    source   = gst_element_factory_make("nvarguscamerasrc", "source");
    conv     = gst_element_factory_make("nvvidconv", "converter");
    filter   = gst_element_factory_make("capsfilter", "filter");
    enc      = gst_element_factory_make("nvv4l2h264enc", "encoder");
    pay      = gst_element_factory_make("rtph264pay", "payloader");
    sink     = gst_element_factory_make("udpsink", "sink");

    if (!pipeline || !source || !conv || !filter || !enc || !pay || !sink) {
        g_printerr("Failed to create one or more elements.\n");
        return -1;
    }

    // Configurar elementos
    GstCaps *caps = gst_caps_from_string("video/x-raw(memory:NVMM), format=NV12, width=1280, height=720");
    g_object_set(G_OBJECT(filter), "caps", caps, NULL);
    gst_caps_unref(caps);

    g_object_set(G_OBJECT(enc), "insert-sps-pps", TRUE, "bitrate", 4000000, NULL);
    g_object_set(G_OBJECT(pay), "config-interval", 1, "pt", 96, NULL);
    g_object_set(G_OBJECT(sink), "host", "192.168.12.193", "port", 5000, NULL);

    // Añadir al pipeline y enlazar
    gst_bin_add_many(GST_BIN(pipeline), source, conv, filter, enc, pay, sink, NULL);
    if (!gst_element_link_many(source, conv, filter, enc, pay, sink, NULL)) {
        g_printerr("Error linking elements\n");
        return -1;
    }

    // Configurar bus de mensajes
    bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    bus_watch_id = gst_bus_add_watch(bus, bus_call, loop);
    gst_object_unref(bus);

    // Ejecutar
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    g_print("Streaming video to 192.168.12.193:5000...\n");
    g_main_loop_run(loop);

    // Finalizar
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    g_source_remove(bus_watch_id);
    g_main_loop_unref(loop);

    return 0;
}


