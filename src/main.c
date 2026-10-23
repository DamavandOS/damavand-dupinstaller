#include <adwaita.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <pango/pango.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UPDATER_PATH "/vendor/usr/bin/damavand-update"
#define APP_ID "org.damavandos.Updater"

typedef struct {
    GtkApplication *app;
    AdwApplicationWindow *window;

    GtkLabel *file_label;
    GtkLabel *status_label;
    GtkButton *choose_button;
    GtkButton *install_button;
    GtkButton *logs_button;
    GtkSpinner *spinner;

    char *package_path;
    char *logs;
    gboolean installing;
} UpdaterApp;

static UpdaterApp ui = {0};

static void
set_logs(const char *text)
{
    g_free(ui.logs);
    ui.logs = g_strdup(text ? text : "");
}

static void
append_logs(const char *text)
{
    char *combined = g_strconcat(
        ui.logs ? ui.logs : "",
        text ? text : "",
        NULL
    );

    g_free(ui.logs);
    ui.logs = combined;
}

static void
set_status(const char *text)
{
    gtk_label_set_text(ui.status_label, text);
}

static void
set_installing(gboolean installing)
{
    ui.installing = installing;

    gtk_widget_set_sensitive(
        GTK_WIDGET(ui.choose_button), !installing
    );

    gtk_widget_set_sensitive(
        GTK_WIDGET(ui.install_button),
        !installing && ui.package_path != NULL
    );

    if (installing)
        gtk_spinner_start(ui.spinner);
    else
        gtk_spinner_stop(ui.spinner);
}

/* -------------------- Logs window -------------------- */

static void
copy_logs_clicked(GtkButton *button, gpointer user_data)
{
    GtkTextView *view = GTK_TEXT_VIEW(user_data);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(view);

    GtkTextIter start, end;

    gtk_text_buffer_get_bounds(buffer, &start, &end);

    char *text = gtk_text_buffer_get_text(
        buffer, &start, &end, TRUE
    );

    GdkDisplay *display = gtk_widget_get_display(
        GTK_WIDGET(view)
    );

    gdk_clipboard_set_text(
        gdk_display_get_clipboard(display), text
    );

    g_free(text);
}

static void
show_logs_clicked(GtkButton *button, gpointer user_data)
{
    AdwApplicationWindow *parent = ui.window;

    AdwWindow *dialog = ADW_WINDOW(
        adw_window_new()
    );

    gtk_window_set_transient_for(
        GTK_WINDOW(dialog), GTK_WINDOW(parent)
    );

    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_title(GTK_WINDOW(dialog), "Installation logs");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 680, 440);

    AdwToolbarView *toolbar = ADW_TOOLBAR_VIEW(
        adw_toolbar_view_new()
    );

    AdwHeaderBar *header = ADW_HEADER_BAR(
        adw_header_bar_new()
    );

    GtkWidget *header_title = gtk_label_new("Installation logs");

    adw_header_bar_set_title_widget(header, header_title);
    adw_toolbar_view_add_top_bar(toolbar, GTK_WIDGET(header));

    GtkWidget *outer = gtk_box_new(
        GTK_ORIENTATION_VERTICAL, 10
    );

    gtk_widget_set_margin_top(outer, 12);
    gtk_widget_set_margin_bottom(outer, 12);
    gtk_widget_set_margin_start(outer, 12);
    gtk_widget_set_margin_end(outer, 12);

    GtkWidget *scroller = gtk_scrolled_window_new();

    gtk_widget_set_hexpand(scroller, TRUE);
    gtk_widget_set_vexpand(scroller, TRUE);

    GtkWidget *text_view = gtk_text_view_new();

    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(text_view), TRUE);
    gtk_text_view_set_wrap_mode(
        GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD_CHAR
    );

    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(text_view), 10);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(text_view), 10);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(text_view), 10);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(text_view), 10);

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(
        GTK_TEXT_VIEW(text_view)
    );

    gtk_text_buffer_set_text(
        buffer,
        ui.logs && *ui.logs ? ui.logs : "No logs available yet.",
        -1
    );

    gtk_scrolled_window_set_child(
        GTK_SCROLLED_WINDOW(scroller), text_view
    );

    gtk_box_append(GTK_BOX(outer), scroller);

    GtkWidget *copy_button = gtk_button_new_with_label("Copy logs");

    gtk_widget_set_halign(copy_button, GTK_ALIGN_END);

    g_signal_connect(
        copy_button,
        "clicked",
        G_CALLBACK(copy_logs_clicked),
        text_view
    );

    gtk_box_append(GTK_BOX(outer), copy_button);

    adw_toolbar_view_set_content(
        toolbar, outer
    );

    adw_window_set_content(dialog, GTK_WIDGET(toolbar));
    gtk_window_present(GTK_WINDOW(dialog));
}

/* -------------------- Installation process -------------------- */

static void
installation_finished(
    GObject *source,
    GAsyncResult *result,
    gpointer user_data
)
{
    GSubprocess *process = G_SUBPROCESS(source);

    char *stdout_text = NULL;
    char *stderr_text = NULL;
    GError *error = NULL;

    gboolean communicated = g_subprocess_communicate_utf8_finish(
        process,
        result,
        &stdout_text,
        &stderr_text,
        &error
    );

    if (communicated) {
        append_logs(stdout_text ? stdout_text : "");

        if (stderr_text && *stderr_text) {
            append_logs("\n");
            append_logs(stderr_text);
        }

        if (g_subprocess_get_successful(process)) {
            set_status("Completed");
        } else {
            int exit_code = g_subprocess_get_if_exited(process)
                ? g_subprocess_get_exit_status(process)
                : -1;

            char *message = g_strdup_printf(
                "Failed (exit code %d)", exit_code
            );

            set_status(message);
            g_free(message);
        }
    } else {
        append_logs("\nFailed to collect installer output: ");
        append_logs(error ? error->message : "Unknown error");
        append_logs("\n");

        set_status("Failed");
    }

    g_clear_error(&error);
    g_free(stdout_text);
    g_free(stderr_text);

    set_installing(FALSE);

    gtk_widget_set_sensitive(
        GTK_WIDGET(ui.logs_button), TRUE
    );

    g_object_unref(process);
}

static void
install_clicked(GtkButton *button, gpointer user_data)
{
    if (ui.installing || !ui.package_path)
        return;

    if (!g_file_test(UPDATER_PATH, G_FILE_TEST_IS_EXECUTABLE)) {
        char *message = g_strdup_printf(
            "Updater executable not found or not executable: %s\n",
            UPDATER_PATH
        );

        set_logs(message);
        set_status("Failed");

        gtk_widget_set_sensitive(
            GTK_WIDGET(ui.logs_button), TRUE
        );

        g_free(message);
        return;
    }

    char *command_log = g_strdup_printf(
        "$ pkexec %s \"%s\"\n\n",
        UPDATER_PATH,
        ui.package_path
    );

    set_logs(command_log);
    g_free(command_log);

    set_status("Waiting for administrator authentication…");
    gtk_widget_set_sensitive(
        GTK_WIDGET(ui.logs_button), FALSE
    );

    set_installing(TRUE);

    const char *argv[] = {
        "pkexec",
        UPDATER_PATH,
        ui.package_path,
        NULL
    };

    GError *error = NULL;

    GSubprocess *process = g_subprocess_newv(
        argv,
        G_SUBPROCESS_FLAGS_STDOUT_PIPE |
        G_SUBPROCESS_FLAGS_STDERR_MERGE,
        &error
    );

    if (!process) {
        append_logs("Could not start updater: ");
        append_logs(error ? error->message : "Unknown error");
        append_logs("\n");

        set_status("Failed");
        set_installing(FALSE);

        gtk_widget_set_sensitive(
            GTK_WIDGET(ui.logs_button), TRUE
        );

        g_clear_error(&error);
        return;
    }

    g_subprocess_communicate_utf8_async(
        process,
        NULL,
        NULL,
        installation_finished,
        NULL
    );
}

/* -------------------- File selection -------------------- */

static void
file_selected(
    GtkNativeDialog *dialog,
    int response,
    gpointer user_data
)
{
    if (response == GTK_RESPONSE_ACCEPT) {
        GFile *file = gtk_file_chooser_get_file(
            GTK_FILE_CHOOSER(dialog)
        );

        if (file) {
            char *path = g_file_get_path(file);

            if (path && g_str_has_suffix(path, ".dup")) {
                g_free(ui.package_path);
                ui.package_path = g_strdup(path);

                char *basename = g_path_get_basename(path);

                gtk_label_set_text(
                    ui.file_label, basename
                );

                set_status("Package selected");
                set_logs("");

                gtk_widget_set_sensitive(
                    GTK_WIDGET(ui.install_button), TRUE
                );

                gtk_widget_set_sensitive(
                    GTK_WIDGET(ui.logs_button), FALSE
                );

                g_free(basename);
            } else {
                set_status("Please select a .dup file.");
            }

            g_free(path);
            g_object_unref(file);
        }
    }

    g_object_unref(dialog);
}

static void
choose_clicked(GtkButton *button, gpointer user_data)
{
    GtkFileChooserNative *dialog = gtk_file_chooser_native_new(
        "Choose a Damavand Update Package",
        GTK_WINDOW(ui.window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Open",
        "_Cancel"
    );

    GtkFileFilter *filter = gtk_file_filter_new();

    gtk_file_filter_set_name(
        filter, "Damavand update packages (*.dup)"
    );

    gtk_file_filter_add_pattern(filter, "*.dup");

    gtk_file_chooser_add_filter(
        GTK_FILE_CHOOSER(dialog), filter
    );

    g_signal_connect(
        dialog,
        "response",
        G_CALLBACK(file_selected),
        NULL
    );

    gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
}

/* -------------------- Main window -------------------- */

static void
activate(GtkApplication *app, gpointer user_data)
{
    if (ui.window) {
        gtk_window_present(GTK_WINDOW(ui.window));
        return;
    }

    ui.app = app;

    ui.window = ADW_APPLICATION_WINDOW(
        adw_application_window_new(app)
    );

    gtk_window_set_title(
        GTK_WINDOW(ui.window), "DamavandOS Updater"
    );

    gtk_window_set_default_size(
        GTK_WINDOW(ui.window), 520, 350
    );

    AdwToolbarView *toolbar = ADW_TOOLBAR_VIEW(
        adw_toolbar_view_new()
    );

    AdwHeaderBar *header = ADW_HEADER_BAR(
        adw_header_bar_new()
    );

    adw_header_bar_set_title_widget(
        header, gtk_label_new("DamavandOS Updater")
    );

    adw_toolbar_view_add_top_bar(
        toolbar, GTK_WIDGET(header)
    );

    GtkWidget *page = gtk_box_new(
        GTK_ORIENTATION_VERTICAL, 14
    );

    gtk_widget_set_margin_top(page, 28);
    gtk_widget_set_margin_bottom(page, 24);
    gtk_widget_set_margin_start(page, 24);
    gtk_widget_set_margin_end(page, 24);

    GtkWidget *icon = gtk_image_new_from_icon_name(
        "software-update-available-symbolic"
    );

    gtk_image_set_pixel_size(GTK_IMAGE(icon), 48);
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(page), icon);

    GtkWidget *title = gtk_label_new("Install a system update");

    gtk_widget_add_css_class(title, "title-2");
    gtk_widget_set_halign(title, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(page), title);

    GtkWidget *subtitle = gtk_label_new(
        "Choose a .dup package to install it with administrator privileges."
    );

    gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);
    gtk_label_set_justify(GTK_LABEL(subtitle), GTK_JUSTIFY_CENTER);
    gtk_widget_add_css_class(subtitle, "dim-label");
    gtk_widget_set_halign(subtitle, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(page), subtitle);

    ui.file_label = GTK_LABEL(
        gtk_label_new("No update package selected")
    );

    gtk_label_set_ellipsize(
        ui.file_label, PANGO_ELLIPSIZE_MIDDLE
    );

    gtk_label_set_selectable(ui.file_label, TRUE);
    gtk_widget_set_halign(GTK_WIDGET(ui.file_label), GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(GTK_WIDGET(ui.file_label), 8);

    gtk_box_append(GTK_BOX(page), GTK_WIDGET(ui.file_label));

    ui.choose_button = GTK_BUTTON(
        gtk_button_new_with_label("Choose .dup file")
    );

    gtk_widget_add_css_class(
        GTK_WIDGET(ui.choose_button), "pill"
    );

    gtk_widget_set_halign(
        GTK_WIDGET(ui.choose_button), GTK_ALIGN_CENTER
    );

    g_signal_connect(
        ui.choose_button,
        "clicked",
        G_CALLBACK(choose_clicked),
        NULL
    );

    gtk_box_append(GTK_BOX(page), GTK_WIDGET(ui.choose_button));

    ui.install_button = GTK_BUTTON(
        gtk_button_new_with_label("Install Update")
    );

    gtk_widget_add_css_class(
        GTK_WIDGET(ui.install_button), "suggested-action"
    );

    gtk_widget_add_css_class(
        GTK_WIDGET(ui.install_button), "pill"
    );

    gtk_widget_set_halign(
        GTK_WIDGET(ui.install_button), GTK_ALIGN_CENTER
    );

    gtk_widget_set_sensitive(
        GTK_WIDGET(ui.install_button), FALSE
    );

    g_signal_connect(
        ui.install_button,
        "clicked",
        G_CALLBACK(install_clicked),
        NULL
    );

    gtk_box_append(GTK_BOX(page), GTK_WIDGET(ui.install_button));

    ui.spinner = GTK_SPINNER(gtk_spinner_new());

    gtk_widget_set_halign(
        GTK_WIDGET(ui.spinner), GTK_ALIGN_CENTER
    );

    gtk_box_append(GTK_BOX(page), GTK_WIDGET(ui.spinner));

    ui.status_label = GTK_LABEL(gtk_label_new("Ready"));

    gtk_widget_add_css_class(
        GTK_WIDGET(ui.status_label), "dim-label"
    );

    gtk_widget_set_halign(
        GTK_WIDGET(ui.status_label), GTK_ALIGN_CENTER
    );

    gtk_box_append(GTK_BOX(page), GTK_WIDGET(ui.status_label));

    GtkWidget *actions = gtk_box_new(
        GTK_ORIENTATION_HORIZONTAL, 8
    );

    gtk_widget_set_halign(actions, GTK_ALIGN_CENTER);

    ui.logs_button = GTK_BUTTON(
        gtk_button_new_with_label("Show Logs")
    );

    gtk_widget_set_sensitive(
        GTK_WIDGET(ui.logs_button), FALSE
    );

    g_signal_connect(
        ui.logs_button,
        "clicked",
        G_CALLBACK(show_logs_clicked),
        NULL
    );

    gtk_box_append(GTK_BOX(actions), GTK_WIDGET(ui.logs_button));

    GtkWidget *close_button = gtk_button_new_with_label("Close");

    g_signal_connect_swapped(
        close_button,
        "clicked",
        G_CALLBACK(gtk_window_close),
        ui.window
    );

    gtk_box_append(GTK_BOX(actions), close_button);
    gtk_box_append(GTK_BOX(page), actions);

    adw_toolbar_view_set_content(toolbar, page);

    adw_application_window_set_content(
        ui.window, GTK_WIDGET(toolbar)
    );

    gtk_window_present(GTK_WINDOW(ui.window));
}

int
main(int argc, char **argv)
{
    AdwApplication *app = adw_application_new(
        APP_ID, G_APPLICATION_DEFAULT_FLAGS
    );

    adw_init();

    g_signal_connect(
        app, "activate", G_CALLBACK(activate), NULL
    );

    int status = g_application_run(
        G_APPLICATION(app), argc, argv
    );

    g_free(ui.package_path);
    g_free(ui.logs);
    g_object_unref(app);

    return status;
}
