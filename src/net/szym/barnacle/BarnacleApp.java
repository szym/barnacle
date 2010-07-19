/*	
 *  This file is part of Barnacle Wifi Tether
 *  Copyright (C) 2010 by Szymon Jakubczak
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

package net.szym.barnacle;

import java.util.ArrayList;

import android.app.Notification;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager.NameNotFoundException;
import android.preference.PreferenceManager;
import android.util.Log;
import android.widget.Toast;


/**
 * Manages preferences, activities and prepares the service
 */
public class BarnacleApp extends android.app.Application {
    final static String TAG = "BarnacleApp";

    final static String FILE_SCRIPT = "setup";
    final static String FILE_INI    = "brncl.ini";
    
    final static String ACTION_CLIENTS = "net.szym.barnacle.SHOW_CLIENTS";
    
    final static int ERROR_ROOT = 1;
    final static int ERROR_OTHER = 2;

    SharedPreferences prefs;
    private StatusActivity  statusActivity = null;
    private ClientsActivity clientsActivity = null;
    private Toast toast;

    // notifications
    NotificationManager notificationManager;
    private Notification notification;
    private Notification notificationClientAdded;
    final static int NOTIFY_RUNNING = 0;
    final static int NOTIFY_CLIENT = 1;

    public BarnacleService service = null;

    public void onCreate() {
        super.onCreate();
        // initialize default values if not done this in the past
        PreferenceManager.setDefaultValues(this, R.xml.preferences, false);
        prefs = PreferenceManager.getDefaultSharedPreferences(this);
        notificationManager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        toast = Toast.makeText(this, "", Toast.LENGTH_SHORT);
        notification = new Notification(R.drawable.barnacle, getString(R.string.notify_running), 0);
        notification.flags |= Notification.FLAG_ONGOING_EVENT;
        // FIXME: icon
        notificationClientAdded = new Notification(android.R.drawable.stat_sys_warning,
                                                   getString(R.string.notify_client), 0);
        notificationClientAdded.defaults = Notification.DEFAULT_SOUND; // | Notification.DEFAULT_VIBRATE // requires permission
        
        ensureService();
    }

    public void ensureService() {
        if (service != null) return; // ensured
        startService(new Intent(this, BarnacleService.class));
    }
    
    public void setStatusActivity(StatusActivity a) { // for updates
        statusActivity = a;
    }
    public void setClientsActivity(ClientsActivity a) { // for updates
        clientsActivity = a;
    }
    public void setService(BarnacleService s) {
        service = s;
        updateStatus();
    }

    protected void updateStatus() {
        if (statusActivity != null)
            statusActivity.update();
    }

    public void updateToast(String msg, boolean islong) {
        toast.setText(msg);
        toast.setDuration(islong ? Toast.LENGTH_LONG : Toast.LENGTH_SHORT);
        toast.show();
    }

    public void clientAdded(BarnacleService.ClientData cd) {
        Intent notificationIntent = new Intent(this, StatusActivity.class);
        notificationIntent.setAction(ACTION_CLIENTS);
        PendingIntent contentIntent = PendingIntent.getActivity(this, 0, notificationIntent, 0);
        notificationClientAdded.setLatestEventInfo(this, getString(R.string.app_name),
                        getString(R.string.notify_client) + " " + cd.toNiceString(), contentIntent);
        notificationManager.notify(NOTIFY_CLIENT, notificationClientAdded);
        if (clientsActivity != null)
            clientsActivity.update();
    }

    public void serviceStarted() {
        Intent notificationIntent = new Intent(this, StatusActivity.class);
        PendingIntent contentIntent = PendingIntent.getActivity(this, 0, notificationIntent, 0);
        notification.setLatestEventInfo(this, getString(R.string.app_name),
                        getString(R.string.notify_running), contentIntent);
        notificationManager.notify(NOTIFY_RUNNING, notification);
        //service.startForeground(NOTIFY_RUNNING, notification);
    }
    
    public void serviceStopped() {
        notificationManager.cancel(NOTIFY_RUNNING);
        notificationManager.cancel(NOTIFY_CLIENT);
    }
    
    public void failed(int err) {
    	if (statusActivity != null) {
    		if (err == ERROR_ROOT) {
    			statusActivity.showDialog(StatusActivity.DLG_ROOT);
    		} else if (err == ERROR_OTHER) {
    			statusActivity.getTabHost().setCurrentTab(0); // show log
    	    	statusActivity.showDialog(StatusActivity.DLG_ERROR);
    		}
    	}
    }
    
    private boolean unpackRaw(int id, String filename) {
        Log.d(TAG, "unpacking " + id + " to " + filename);
        try {
            getFileStreamPath(filename).delete(); // to ensure that openFileOutput doesn't throw FileNotFound
            java.io.InputStream is = getResources().openRawResource(id);
            java.io.OutputStream os = openFileOutput(filename, MODE_PRIVATE);
            byte [] buffer = new byte[8192];
            int len;
            while((len = is.read(buffer)) >= 0) {
                os.write(buffer, 0, len);
            }
            os.close();
        } catch (Exception e) {
            updateToast("Could not unpack " + filename + ": " + e.getMessage(), true);
            Log.e(TAG, "unpack " + filename + ": " + e + ": " + e.getMessage() + " " + e.getCause());
            return false;
        }
        return true;
    }
    
    private boolean installIfNeeded(boolean newVersion, int resource, String filename) {
    	if(newVersion || !getFileStreamPath(filename).exists()) {
    		// binary does not exist or old, install it
        	return unpackRaw(resource, filename); 
        }
    	return true;
    }

    /** Prepare the binaries */
    protected boolean prepareBinaries() {
        int versionCode = -1;
        try {
            versionCode = getPackageManager().getPackageInfo(getPackageName(), 0).versionCode;
        } catch (NameNotFoundException e) { }
        boolean newVersion = (prefs.getInt("bin.version", 0) != versionCode);

        if (!installIfNeeded(newVersion, R.raw.setup, FILE_SCRIPT)) return false;
        if (!installIfNeeded(newVersion, R.raw.run,   "run")) 	  return false;
        if (!installIfNeeded(newVersion, R.raw.dhcp,  "dhcp")) 	  return false;
        if (!installIfNeeded(newVersion, R.raw.nat,   "nat")) 	  return false;
        if (!installIfNeeded(newVersion, R.raw.wifi,  "wifi"))    return false;
                   
        // unpack all scripts
        String [] scripts = getResources().getStringArray(R.array.script_values);
        //int [] ids = getResources().getIntArray(R.array.script_ids); // doesn't work -- buggo in AOSP
        android.content.res.TypedArray ar = getResources().obtainTypedArray(R.array.script_ids);
        for (int i = 1; i < scripts.length; ++i) { // NOTE: the first one is none
            //int id = ids[i];
            int id = ar.getResourceId(i, 0);
            if (!installIfNeeded(newVersion, id, scripts[i])) return false;
        }
        ar.recycle();
        
        if (Util.exec("chmod 750 " + getFileStreamPath(FILE_SCRIPT)) != 0) {
            updateToast("Could not make the binary executable", true);
            return false;
        }
        prefs.edit().putInt("bin.version", versionCode).commit(); // installed
        return true;
    }

    protected String natCtrlPath() {
    	return getFileStreamPath("nat_ctrl").getPath();
    }
    
    /** Prepare .ini file from preferences */
    protected boolean prepareIni() {
        StringBuilder sb = new StringBuilder();
        final int[] ids = SettingsActivity.prefids;
        for (int i = 0; i < ids.length; ++i) {
            String k = getString(ids[i]);
            String v = prefs.getString(k, null);
            if (v != null && v.length() != 0) {
                if (ids[i] == R.string.lan_essid) {
                    v = '"'+v+'"';
                }
                if (ids[i] == R.string.lan_wep) {
                    String pass = WEPPreference.getAsciiContent(v);
                    if (pass != null) {
                        v = Util.asc2hex(pass);
                    }
                }
                sb.append("brncl_").append(k).append('=').append(v).append('\n');
            }
        }
        // not included in prefids are checkboxes
        final int[] checks = SettingsActivity.checks;
        for (int i = 0; i < checks.length; ++i) {
            String k = getString(checks[i]);
            if (prefs.getBoolean(k, false))
                sb.append("brncl_").append(k).append("=1\n");
        }
        
        // only enable nat_ctrl if filtering is enabled
        // FIXME: this should not be tied like this 
        if (prefs.getBoolean(getString(R.string.nat_filter), false)) {
        	sb.append("brncl_nat_ctrl=").append(natCtrlPath());
        }
        
        if (sb.length() == 0) {
            // wow, no preferences?
            updateToast("Please, set your preferences", false);
            try {
                PreferenceManager.setDefaultValues(this, R.xml.preferences, true);
                statusActivity.startActivity(new Intent(this, SettingsActivity.class));
            } catch (Exception e) {
                Log.e(TAG, e.toString());
                //updateToast("WOAH! " + e, true);
            }
            return false;
        }
        try {
            java.io.OutputStream os = openFileOutput(FILE_INI, MODE_PRIVATE);
            os.write(sb.toString().getBytes());
            os.close();
            return true;
        } catch (java.io.IOException e) {
            return false;
        }
    }

    /** find default route interface */
    protected boolean prepareIfWan() {
        String if_wan = prefs.getString(getString(R.string.if_wan), "");
        if (if_wan.length() != 0) return true;

        // must find mobile data interface
        ArrayList<String> routes = Util.readLinesFromFile("/proc/net/route");
        for (int i = 1; i < routes.size(); ++i) {
            String line = routes.get(i);
            String[] tokens = line.split("\\s+");
            if (tokens[1].equals("00000000")) {
                // this is our default route
                if_wan = tokens[0];
                break;
            }
        }
        if (if_wan.length() != 0) {
            updateToast("Mobile data interface found: " + if_wan, false);
            prefs.edit().putString(getString(R.string.if_wan), if_wan).commit();
            return true;
        }
        return false;
    }

    protected void foundIfLan(String found_if_lan) {
        String if_lan = prefs.getString(getString(R.string.if_lan), "");
        if (if_lan.length() == 0) {
            updateToast("Wireless interface found: " + found_if_lan, false);
        }
        // NOTE: always use the name found by the process
        if_lan = found_if_lan;
        prefs.edit().putString(getString(R.string.if_lan), if_lan).commit();
    }
}

