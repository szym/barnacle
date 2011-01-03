package net.szym.barnacle;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

public class ToggleReceiver extends BroadcastReceiver {
    final static String TAG = "Barnacle.ToggleReceiver";
    @Override
    public void onReceive(Context context, Intent intent) {
        Log.d(TAG, "onReceive " + intent.getAction());
        if (BarnacleApp.ACTION_TOGGLE.equals(intent.getAction())) {
            // potential race conditions, but they are benign
            BarnacleService service = BarnacleService.singleton;
            //Log.d(TAG, "service " + ((service == null) ? "null" : "present"));
            if (service != null) {
                if (!intent.getBooleanExtra("start", false)) {
                    Log.d(TAG, "stop");
                    service.stopRequest();
                }
            } else {
                if (intent.getBooleanExtra("start", true)) {
                    Log.d(TAG, "start");
                    context.startService(new Intent(context, BarnacleService.class));
                }
            }
        } else if (BarnacleApp.ACTION_CHECK.equals(intent.getAction())) {
            // FIXME: this is the most inefficient way of finding out the state
            BarnacleService service = BarnacleService.singleton;
            int state = (service != null) ? service.getState() : BarnacleService.STATE_STOPPED;
            BarnacleApp.broadcastState(context, state);
        }
    }
}
