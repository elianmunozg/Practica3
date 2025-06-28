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
    GstElement *pipeline, *source, *queue1, *queue2, *queue3;
    GstElement *streammux, *nvvidconv1, *nvinfer, *nvdsosd, *nvvidconv2;
    GstElement *encoder, *identity, *parser, *payloader, *sink;
    GstBus *bus;
    guint bus_watch_id;

    gst_init(&argc, &argv);
    loop = g_main_loop_new(NULL, FALSE);

    pipeline = gst_pipeline_new("deepstream-pipeline");

    // Crear todos los elementos
    source      = gst_element_factory_make("nvarguscamerasrc", "camera-source");
    queue1      = gst_element_factory_make("queue", "queue1");
    streammux   = gst_element_factory_make("nvstreammux", "stream-muxer");
    queue2      = gst_element_factory_make("queue", "queue2");
    nvvidconv1  = gst_element_factory_make("nvvideoconvert", "nvvidconv1");
    nvinfer     = gst_element_factory_make("nvinfer", "primary-nvinference-engine");
    queue3      = gst_element_factory_make("queue", "queue3");
    nvdsosd     = gst_element_factory_make("nvdsosd", "nv-onscreendisplay");
    nvvidconv2  = gst_element_factory_make("nvvideoconvert", "nvvidconv2");
    encoder     = gst_element_factory_make("nvv4l2h264enc", "h264-encoder");
    identity    = gst_element_factory_make("identity", "identity");
    parser      = gst_element_factory_make("h264parse", "h264-parser");
    payloader   = gst_element_factory_make("rtph264pay", "rtp-payloader");
    sink        = gst_element_factory_make("udpsink", "udp-sink");

    if (!pipeline || !source || !queue1 || !streammux || !queue2 || !nvvidconv1 || !nvinfer || 
        !queue3 || !nvdsosd || !nvvidconv2 || !encoder || !identity || !parser || !payloader || !sink) {
        g_printerr("Failed to create one or more elements.\n");
        return -1;
    }

    // Configuración de elementos
    g_object_set(G_OBJECT(source), "bufapi-version", TRUE, NULL);
    g_object_set(G_OBJECT(streammux), 
                 "width", 1920, "height", 1080, "batch-size", 1, 
                 "batched-push-timeout", 40000, NULL);
    g_object_set(G_OBJECT(nvinfer), 
                 "config-file-path", "/opt/nvidia/deepstream/deepstream-6.0/samples/configs/deepstream-app/config_infer_primary.txt",
                 "model-engine-file", "/opt/nvidia/deepstream/deepstream-6.0/samples/models/Primary_Detector/resnet10.caffemodel_b1_gpu0_fp16.engine",
                 NULL);
    g_object_set(G_OBJECT(nvdsosd), "process-mode", 0, NULL); // HW_MODE = 0
    g_object_set(G_OBJECT(encoder), "insert-sps-pps", TRUE, NULL);
    g_object_set(G_OBJECT(identity), "silent", FALSE, NULL);
    g_object_set(G_OBJECT(payloader), "pt", 96, NULL);
    g_object_set(G_OBJECT(sink), "host", "192.168.0.109", "port", 5000, NULL);

    // Añadir todos los elementos al pipeline
    gst_bin_add_many(GST_BIN(pipeline), 
        source, queue1, streammux, queue2, nvvidconv1, nvinfer,
        queue3, nvdsosd, nvvidconv2, encoder, identity, parser,
        payloader, sink, NULL);

    // Enlace: fuente → cola → mux.sink_0
    GstPad *sinkpad, *srcpad;
    gst_element_link(source, queue1);

    srcpad = gst_element_get_static_pad(queue1, "src");
    sinkpad = gst_element_get_request_pad(streammux, "sink_0");
    if (gst_pad_link(srcpad, sinkpad) != GST_PAD_LINK_OK) {
        g_printerr("Failed to link camera queue to streammux.\n");
        return -1;
    }
    gst_object_unref(srcpad);
    gst_object_unref(sinkpad);

    // Enlace: mux → resto del pipeline
    if (!gst_element_link_many(streammux, queue2, nvvidconv1, nvinfer, queue3,
                               nvdsosd, nvvidconv2, encoder, identity, parser,
                               payloader, sink, NULL)) {
        g_printerr("Failed to link elements in pipeline.\n");
        return -1;
    }

    // Configurar bus y bucle principal
    bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    bus_watch_id = gst_bus_add_watch(bus, bus_call, loop);
    gst_object_unref(bus);

    // Ejecutar pipeline
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    g_print("Streaming video with DeepStream to 192.168.0.113:5000...\n");
    g_main_loop_run(loop);

    // Limpieza
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    g_source_remove(bus_watch_id);
    g_main_loop_unref(loop);

    return 0;
}

