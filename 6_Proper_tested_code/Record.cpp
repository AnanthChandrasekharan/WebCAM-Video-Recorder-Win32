#include <gst/gst.h>
#include <stdio.h>
#include <conio.h>  // For detecting key press on Windows
#include <time.h>   // For timestamp

#pragma comment(lib, "C:\\gstreamer\\1.0\\msvc_x86_64\\lib\\gstreamer-1.0.lib")
#pragma comment(lib, "C:\\gstreamer\\1.0\\msvc_x86_64\\lib\\glib-2.0.lib")
#pragma comment(lib, "C:\\gstreamer\\1.0\\msvc_x86_64\\lib\\gobject-2.0.lib")

GstElement *pipeline, *source, *filter, *enc, *mux, *filesink;
GstBus *bus;
gboolean is_recording = FALSE;
gboolean eos_received = FALSE;

void get_timestamped_filename(char *filename) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(filename, 100, "output_%Y-%m-%d_%H-%M-%S.mp4", t);
}

gboolean on_message(GstBus *bus, GstMessage *message, gpointer user_data) {
    GError *err;
    gchar *debug;

    switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_EOS:
            printf("[LOG] End-Of-Stream reached. Finalizing file.\n");
            eos_received = TRUE;
            break;
        case GST_MESSAGE_ERROR:
            gst_message_parse_error(message, &err, &debug);
            g_printerr("[ERROR] %s\n", err->message);
            g_printerr("[DEBUG] %s\n", debug ? debug : "none");
            g_error_free(err);
            g_free(debug);
            eos_received = TRUE;
            break;
        case GST_MESSAGE_STATE_CHANGED:
            if (GST_MESSAGE_SRC(message) == GST_OBJECT(pipeline)) {
                GstState old_state, new_state, pending_state;
                gst_message_parse_state_changed(message, &old_state, &new_state, &pending_state);
                printf("[LOG] Pipeline state changed from %s to %s.\n",
                       gst_element_state_get_name(old_state), gst_element_state_get_name(new_state));
            }
            break;
        default:
            break;
    }
    return TRUE;
}

void start_recording() {
    if (!is_recording) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        printf("[LOG] Pipeline set to NULL. Preparing to start recording.\n");

        char filename[100];
        get_timestamped_filename(filename);
        printf("[LOG] Saving file to: %s\n", filename);

        g_object_set(G_OBJECT(filesink), "location", filename, NULL);

        GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
        if (ret == GST_STATE_CHANGE_FAILURE) {
            g_printerr("[ERROR] Failed to start recording. Pipeline state change failed.\n");
            return;
        }

        printf("[LOG] Recording started.\n");
        is_recording = TRUE;
        eos_received = FALSE;  // Reset EOS flag for new recording
    } else {
        printf("[LOG] Already recording.\n");
    }
}

void stop_recording() {
    if (is_recording) {
        printf("[LOG] Stopping recording. Sending EOS...\n");
        gst_element_send_event(pipeline, gst_event_new_eos());

        // Wait for the EOS message to be processed fully
        while (!eos_received) {
            g_main_context_iteration(NULL, FALSE);
        }

        printf("[LOG] EOS received. Finalizing file...\n");

        // Allow the muxer to finalize the MP4 file
        GstStateChangeReturn ret = gst_element_get_state(pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
        if (ret == GST_STATE_CHANGE_FAILURE) {
            g_printerr("[ERROR] Pipeline state retrieval failed during finalization.\n");
        }

        // Set pipeline to NULL to fully stop it after EOS is processed
        gst_element_set_state(pipeline, GST_STATE_NULL);
        printf("[LOG] Recording stopped. Pipeline set to NULL.\n");

        is_recording = FALSE;
    } else {
        printf("[LOG] No active recording to stop.\n");
    }
}

void init_gstreamer_pipeline() {
    gst_init(NULL, NULL);

    printf("[LOG] Initializing GStreamer pipeline...\n");

    pipeline = gst_pipeline_new("webcam-pipeline");
    source = gst_element_factory_make("mfvideosrc", "webcam-source");
    if (!source) {
        printf("[ERROR] mfvideosrc not available. Using ksvideosrc.\n");
        source = gst_element_factory_make("ksvideosrc", "webcam-source");
    }

    filter = gst_element_factory_make("capsfilter", "filter");
    enc = gst_element_factory_make("x264enc", "h264-encoder");
    mux = gst_element_factory_make("qtmux", "qt-muxer");
    filesink = gst_element_factory_make("filesink", "file-output");

    if (!pipeline || !source || !filter || !enc || !mux || !filesink) {
        printf("[ERROR] Failed to create elements.\n");
        return;
    }

    GstCaps *caps = gst_caps_from_string("video/x-raw,width=640,height=480,framerate=30/1");
    g_object_set(G_OBJECT(filter), "caps", caps, NULL);
    gst_caps_unref(caps);

    gst_bin_add_many(GST_BIN(pipeline), source, filter, enc, mux, filesink, NULL);

    if (!gst_element_link_many(source, filter, enc, mux, filesink, NULL)) {
        g_printerr("[ERROR] Failed to link elements in the pipeline.\n");
        gst_object_unref(pipeline);
        return;
    }

    bus = gst_element_get_bus(pipeline);
    gst_bus_add_watch(bus, (GstBusFunc)on_message, NULL);
}

int main(int argc, char *argv[]) {
    gchar key;

    init_gstreamer_pipeline();
    printf("[LOG] Press 'a' to start recording, 'b' to stop, 'c' to exit.\n");

    while (1) {
        key = _getch();

        if (key == 'a') {
            start_recording();
        } else if (key == 'b') {
            stop_recording();
        } else if (key == 'c') {
            if (is_recording) {
                stop_recording();
            }
            break;
        }

        while (gst_bus_have_pending(bus)) {
            GstMessage *msg = gst_bus_pop(bus);
            if (msg) {
                on_message(bus, msg, NULL);
                gst_message_unref(msg);
            }
        }

        g_main_context_iteration(NULL, FALSE);
    }

    printf("[LOG] Cleaning up...\n");
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    gst_object_unref(bus);
    printf("[LOG] Application terminated.\n");

    return 0;
}
