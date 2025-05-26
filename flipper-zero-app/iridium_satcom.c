#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_input.h>
#include <gui/modules/widget.h>
#include <notification/notification_messages.h>

#define TAG "IridiumSatcom"

typedef enum {
    IridiumSatcomViewSubmenu,
    IridiumSatcomViewWidget,
} IridiumSatcomView;

typedef struct {
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    Widget* widget;
    NotificationApp* notifications;
} IridiumSatcom;

// Forward declarations
static void iridium_app_submenu_callback(void* context, uint32_t index);
static uint32_t iridium_app_exit_callback(void* context);

// Menu item callbacks
static void iridium_app_hello_world_callback(void* context, uint32_t index) {
    UNUSED(index);
    IridiumSatcom* app = context;
    
    // Clear widget and add hello world text
    widget_reset(app->widget);
    widget_add_text_box_element(app->widget, 0, 0, 50, 50, AlignCenter, AlignCenter, "Hello", 1);
   
    // Show notification
    notification_message(app->notifications, &sequence_success);
    
    // Switch to widget view
    view_dispatcher_switch_to_view(app->view_dispatcher, IridiumSatcomViewWidget);
}

static void iridium_app_led_test_callback(void* context, uint32_t index) {
    UNUSED(index);
    IridiumSatcom* app = context;
    
    // Clear widget and add LED test text
    widget_reset(app->widget);
    widget_add_string_element(app->widget, 64, 15, AlignCenter, AlignCenter, FontPrimary, "LED Test");
    widget_add_string_element(app->widget, 64, 30, AlignCenter, AlignCenter, FontSecondary, "Red LED blinking...");
    widget_add_string_element(app->widget, 64, 45, AlignCenter, AlignCenter, FontSecondary, "Press Back to return");
    
    // Blink red LED
    notification_message(app->notifications, &sequence_blink_red_100);
    
    // Switch to widget view
    view_dispatcher_switch_to_view(app->view_dispatcher, IridiumSatcomViewWidget);
}

static void iridium_app_vibrate_callback(void* context, uint32_t index) {
    UNUSED(index);
    IridiumSatcom* app = context;
    
    // Clear widget and add vibration text
    widget_reset(app->widget);
    widget_add_string_element(app->widget, 64, 20, AlignCenter, AlignCenter, FontPrimary, "Vibration Test");
    widget_add_string_element(app->widget, 64, 35, AlignCenter, AlignCenter, FontSecondary, "Buzz!");
    widget_add_string_element(app->widget, 64, 50, AlignCenter, AlignCenter, FontSecondary, "Press Back to return");
    
    // Vibrate
    notification_message(app->notifications, &sequence_single_vibro);
    
    // Switch to widget view
    view_dispatcher_switch_to_view(app->view_dispatcher, IridiumSatcomViewWidget);
}

// Submenu callback
static void iridium_app_submenu_callback(void* context, uint32_t index) {
    //IridiumSatcom* app = context;
    switch(index) {
        case 0:
            iridium_app_hello_world_callback(context, index);
            break;
        case 1:
            iridium_app_led_test_callback(context, index);
            break;
        case 2:
            iridium_app_vibrate_callback(context, index);
            break;
    }
}

// Exit callback
static uint32_t iridium_app_exit_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

// Back callback for widget
static uint32_t iridium_app_widget_back_callback(void* context) {
    //IridiumSatcom* app = context;
    UNUSED(context);
    return IridiumSatcomViewSubmenu;
}

// Allocate app
static IridiumSatcom* iridium_app_alloc() {
    IridiumSatcom* app = malloc(sizeof(IridiumSatcom));
    
    // Initialize view dispatcher
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_enable_queue(app->view_dispatcher);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    
    // Initialize submenu
    app->submenu = submenu_alloc();
    submenu_add_item(app->submenu, "Signal Check", 0, iridium_app_submenu_callback, app);
    submenu_add_item(app->submenu, "Send Message", 1, iridium_app_submenu_callback, app);
    submenu_add_item(app->submenu, "Interface Hub", 2, iridium_app_submenu_callback, app);
    
    View* submenu_view = submenu_get_view(app->submenu);
    view_set_previous_callback(submenu_view, iridium_app_exit_callback);
    view_dispatcher_add_view(app->view_dispatcher, IridiumSatcomViewSubmenu, submenu_view);
    
    // Initialize widget
    app->widget = widget_alloc();
    View* widget_view = widget_get_view(app->widget);
    view_set_previous_callback(widget_view, iridium_app_widget_back_callback);
    view_dispatcher_add_view(app->view_dispatcher, IridiumSatcomViewWidget, widget_view);
    
    // Initialize notifications
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    
    return app;
}

// Free app
static void iridium_app_free(IridiumSatcom* app) {
    furi_assert(app);
    
    // Free views
    view_dispatcher_remove_view(app->view_dispatcher, IridiumSatcomViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, IridiumSatcomViewWidget);
    
    // Free submenu
    submenu_free(app->submenu);
    
    // Free widget
    widget_free(app->widget);
    
    // Close notifications
    furi_record_close(RECORD_NOTIFICATION);
    
    // Free view dispatcher
    view_dispatcher_free(app->view_dispatcher);
    
    // Free app
    free(app);
}

// Main app function
int32_t iridium_satcom_app(void* p) {
    UNUSED(p);
    
    FURI_LOG_I(TAG, "Starting iridium App");
    
    // Allocate app
    IridiumSatcom* app = iridium_app_alloc();
    
    // Get GUI
    Gui* gui = furi_record_open(RECORD_GUI);
    view_dispatcher_attach_to_gui(app->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    
    // Switch to submenu view
    view_dispatcher_switch_to_view(app->view_dispatcher, IridiumSatcomViewSubmenu);
    
    // Run view dispatcher
    view_dispatcher_run(app->view_dispatcher);
    
    // Cleanup
    furi_record_close(RECORD_GUI);
    iridium_app_free(app);
    
    FURI_LOG_I(TAG, "iridium App finished");
    
    return 0;
}