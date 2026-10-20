import android.app.Notification;
import android.app.NotificationManager;
import android.content.Context;
import android.os.Looper;
import android.service.notification.StatusBarNotification;

/** Temporary app_process helper: no APK, channel creation or settings changes. */
public final class PanelUiProbe {
    public static void main(String[] args) throws Exception {
        Looper.prepareMainLooper();
        Class<?> at = Class.forName("android.app.ActivityThread");
        Object thread = at.getMethod("systemMain").invoke(null);
        Context system = (Context) at.getMethod("getSystemContext").invoke(thread);
        Context context = system.createPackageContext("com.android.systemui", 0);
        NotificationManager manager = context.getSystemService(NotificationManager.class);
        final String tag = "gxr-panel-probe-20261020";
        final int id = 20261020;
        if (args.length > 0 && args[0].equals("cancel")) {
            manager.cancel(tag, id);
            return;
        }
        if (manager.getNotificationChannel("SCN_HEADSUP") == null)
            throw new IllegalStateException("Existing silent channel missing");
        Notification notification = new Notification.Builder(context, "SCN_HEADSUP")
            .setSmallIcon(android.R.drawable.ic_dialog_info)
            .setContentTitle("SystemUI optimization test")
            .setContentText("Temporary local test; automatically removed.")
            .setTimeoutAfter(8000).setOnlyAlertOnce(true).build();
        try {
            manager.notify(tag, id, notification);
            Thread.sleep(500);
            boolean found = false;
            for (StatusBarNotification n : manager.getActiveNotifications())
                if (tag.equals(n.getTag()) && n.getId() == id) found = true;
            if (!found) throw new IllegalStateException("Test notification not active");
            System.out.println("POSTED");
            Thread.sleep(4500);
        } finally {
            manager.cancel(tag, id);
            System.out.println("CANCELLED");
        }
    }
}
