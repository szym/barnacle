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

import java.io.DataOutputStream;
import java.util.ArrayList;

import android.content.Context;
import android.content.Intent;
import android.net.ConnectivityManager;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.net.wifi.WifiManager;
import android.os.Handler;
import android.os.IBinder;
import android.os.Message;
import android.util.Log;

/**
 * Manages the running process, client list, and log
 */
public class BarnacleService extends android.app.Service {

    // messages from the process
    final static int MSG_OUTPUT     = 1;
    final static int MSG_ERROR      = 2;
    // messages from self
    final static int MSG_EXCEPTION  = 3;
    final static int MSG_NETSREADY  = 4;
    // requests from activities
    final static int MSG_START      = 5;
    final static int MSG_STOP       = 6;
    final static int MSG_ASSOC      = 7;
    final static int MSG_STATS      = 8;
    final static int MSG_FILTER     = 9;
    // app states
    final static int STATE_STOPPED  = 0;
    final static int STATE_STARTING = 1;
    final static int STATE_RUNNING  = 2; // process said OK

    // private state
    private int state = STATE_STOPPED;
    private Process process = null; // the barnacle process
    private LocalSocket nat_ctrl = null;
    // output monitoring threads
    private Thread[] threads = new Thread[2];
    private String if_lan = ""; // for convenience

    // public state
    public final Util.StyledStringBuilder log = new Util.StyledStringBuilder();

    final static int COLOR_ERROR    = 0xffff2222;
    final static int COLOR_LOG      = 0xff888888;//android.R.color.primary_text_dark;
    final static int COLOR_TIME     = 0xffffffff;

    public static class ClientData {
        final String mac;
        final String ip;
        final String hostname;
        boolean allowed;
        ClientData(String m, String i, String h) { mac = m; ip = i; hostname = h; allowed = false; }
        public String toString() { return mac + " " + ip + " " + hostname; }
        public String toNiceString() { return hostname != null ? hostname : mac; }
    }
    public final ArrayList<ClientData> clients = new ArrayList<ClientData>();
    public final Util.TrafficStats stats = new Util.TrafficStats();

    // convenience
    private BarnacleApp app;
    private WifiManager wifiManager;
    private ConnectivityManager connManager;

    /** public service interface */
    public void startRequest() {
        mHandler.sendEmptyMessage(MSG_START);
    }

    public void assocRequest() {
        mHandler.sendEmptyMessage(MSG_ASSOC);
    }

    public void filterRequest(String mac, boolean allowed) {
        mHandler.obtainMessage(MSG_FILTER, (allowed ? "MACA " : "MACD ") + mac).sendToTarget();
    }

    public void stopRequest() {
        mHandler.sendEmptyMessage(MSG_STOP);
    }

    public void statsRequest(long delay) {
        Message msg = mHandler.obtainMessage(MSG_STATS);
        mHandler.sendMessageDelayed(msg, delay);
    }

    // NOTE: these are subject to race conditions!
    public boolean isRunning() {
        return state == STATE_RUNNING;
    }
    public boolean isChanging() {
        return state == STATE_STARTING;
    }
    public boolean hasFiltering() {
    	return nat_ctrl != null;
    }

    @Override
    public void onCreate() {
        super.onCreate();
        wifiManager = (WifiManager) getSystemService(Context.WIFI_SERVICE);
        connManager = (ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);

        app = (BarnacleApp)getApplication();
        app.setService(this);
    }

    @Override
    public void onDestroy() {
        if (state != STATE_STOPPED)
            Log.e(BarnacleApp.TAG, "BarnacleService destroyed while running!");
        stopProcess(); // ensure we clean up
        super.onDestroy();
    }

    // our handler
    private final Handler mHandler = new Handler() {
        @Override
        public void handleMessage(Message msg) { handle(msg); }
    };

    private void handle(Message msg) {
        switch (msg.what) {
        case MSG_EXCEPTION:
            if (state == STATE_STOPPED) return;
            Throwable thr = (Throwable)msg.obj;
            log(true, getString(R.string.exception) + " " + thr.getMessage());
            Log.e(BarnacleApp.TAG, "Exception " + thr.getMessage() + " " + Log.getStackTraceString(thr));
            stopProcess();
            state = STATE_STOPPED;
            break;
        case MSG_ERROR:
            if (state == STATE_STOPPED) return;
            if (msg.obj != null) {
                log(true, (String)msg.obj); // just dump it and ignore it
            } else {
                log(true, getString(R.string.unexpected));
                stopProcess();
                app.failed(BarnacleApp.ERROR_OTHER);
                state = STATE_STOPPED;
            }
            break;
        case MSG_OUTPUT:
            if (state == STATE_STOPPED) return;
            String line = (String)msg.obj;
            if (line == null) {
                // ignore it, wait for MSG_ERROR(null)
                break;
            }
            if (line.startsWith("DHCPACK")) {
                String[] vals = line.split(" +");
                ClientData cd = new ClientData(vals[1], vals[2], vals.length > 3 ? vals[3] : null);
                clientAdded(cd);
            } else if (line.startsWith("OK")) {
                if_lan = line.split(" +")[1];
                if (state == STATE_STARTING) {
                    // FIXME later: we should always connect to NAT, not just if filtering is enabled
                    if (app.prefs.getBoolean(getString(R.string.nat_filter), false)) {
                        connectToNat();
                    }

                    state = STATE_RUNNING;
                    log(false, getString(R.string.running));
                    clients.clear();
                    stats.init(Util.fetchTrafficData(if_lan));
                    app.foundIfLan(if_lan);
                    app.serviceStarted();
                    mHandler.sendEmptyMessage(MSG_ASSOC);
                }
            } else { // paste to log
                log(false, line);
            }
            break;
        case MSG_NETSREADY:
            if (state == STATE_STARTING) {
                log(false, getString(R.string.dataready));
                if (!app.prepareIfWan()) {
                    log(true, getString(R.string.wanerr));
                    state = STATE_STOPPED;
                    break;
                }
                if (!app.prepareIni()) {
                    log(true, getString(R.string.inierr));
                    state = STATE_STOPPED;
                    break;
                }
                log(false, getString(R.string.iniok));
                if (!startProcess()) {
                    log(true, getString(R.string.starterr));
                    state = STATE_STOPPED;
                    break;
                }
            }
            break;
        case MSG_START:
            if (state != STATE_STOPPED) return;
            log.clear();
            log(false, getString(R.string.starting));

            if (!app.prepareBinaries()) {
                log(true, getString(R.string.unpackerr));
                state = STATE_STOPPED;
                break;
            }

            state = STATE_STARTING;
            prepareNets();
            break;
        case MSG_STOP:
            if (state == STATE_STOPPED) return;
            stopProcess();
            log(false, getString(R.string.stopped));
            state = STATE_STOPPED;
            break;
        case MSG_ASSOC:
            if (state != STATE_RUNNING) return;
            if (tellProcess("WLAN")) {
                app.updateToast(getString(R.string.beaconing), true);
            }
            break;
        case MSG_FILTER:
            if (state != STATE_RUNNING) return;
            if (tellNat((String)msg.obj)) {
                app.updateToast(getString(R.string.filterupdated), false);
            }
        case MSG_STATS:
            mHandler.removeMessages(MSG_STATS); // FIXME: is this the best way of doing this?
            if (state != STATE_RUNNING || if_lan.length() == 0) return;
            stats.update(Util.fetchTrafficData(if_lan));
            break;
        }
        app.updateStatus();
    }

    protected void log(boolean error, String msg) {
        android.text.format.Time time = new android.text.format.Time();
        time.setToNow();
        log.append(COLOR_TIME, time.format("%H:%M:%S\t"))
           .append(error ? COLOR_ERROR : COLOR_LOG, msg)
           .append("\n");
    }

    /** Worker Threads */
    private class OutputMonitor implements Runnable {
        private final java.io.BufferedReader br;
        private final int msg;
        public OutputMonitor(int t, java.io.InputStream is) {
            br = Util.toReader(is);
            msg = t;
        }
        public void run() {
            try{
                String line;
                do {
                    line = br.readLine();
                    mHandler.obtainMessage(msg, line).sendToTarget(); // NOTE: the last null is also sent!
                } while(line != null);
            } catch (Exception e) {
                mHandler.obtainMessage(MSG_EXCEPTION, e).sendToTarget();
            }
        }
    }

    private void clientAdded(ClientData cd) {
        for (int i = 0; i < clients.size(); ++i) {
            ClientData c = clients.get(i);
            if (c.mac.equals(cd.mac)) {
                if (c.ip.equals(cd.ip)) {
                    log(false, String.format(getString(R.string.renewed),
                                             cd.toNiceString()));
                    return; // no change
                }
                cd.allowed = c.allowed;
                clients.remove(i); // we'll add it at the end
                break;
            }
        }
        clients.add(cd);
        log(false, String.format(getString(R.string.connected),
                cd.toNiceString()));
        app.clientAdded(cd);
    }

    private boolean checkNets() {
        return (wifiManager.pingSupplicant() == false) &&
               (wifiManager.getWifiState() == WifiManager.WIFI_STATE_DISABLED) &&
               connManager.getNetworkInfo(ConnectivityManager.TYPE_MOBILE).isConnected();
    }

    private void prepareNets() {
        if (!checkNets()) {
            app.updateToast(getString(R.string.disablewifi), false);
            wifiManager.setWifiEnabled(false);
            String[] actions = {WifiManager.WIFI_STATE_CHANGED_ACTION, ConnectivityManager.CONNECTIVITY_ACTION};
            new Util.WaitingReceiver(actions) {
                boolean run() {
                    if (state != STATE_STARTING) return true; // die silently
                    if (checkNets()) {
                        mHandler.sendEmptyMessage(MSG_NETSREADY);
                        return true;
                    }
                    wifiManager.setWifiEnabled(false);
                    return false;
                }
            }.register(this);
            return;
        }
        mHandler.sendEmptyMessage(MSG_NETSREADY);
    }

    private boolean startProcess() {
        // start the process
        //String [] cmd = {"su", "-c", getFileStreamPath(BarnacleApp.FILE_SCRIPT).getAbsolutePath() };
        //String [] cmd = { getFileStreamPath(BarnacleApp.FILE_SCRIPT) };
        try {
            ProcessBuilder pb = new ProcessBuilder();
            pb.command("./" + BarnacleApp.FILE_SCRIPT).directory(getFilesDir());
            // TODO: consider putting brncl.ini in pb.environment() instead of using ./setup
            process = pb.start(); //Runtime.getRuntime().exec(cmd);
            threads[0] = new Thread(new OutputMonitor(MSG_OUTPUT, process.getInputStream()));
            threads[1] = new Thread(new OutputMonitor(MSG_ERROR, process.getErrorStream()));
            threads[0].start();
            threads[1].start();
        } catch (Exception e) {
            // FIXME: this code is dead because FILE_SCRIPT runs su
            log(true, "su didn't work, do you have root?");
            Log.e(BarnacleApp.TAG, "su failed " + e.toString());
            app.failed(BarnacleApp.ERROR_ROOT);
            return false;
        }
        return true;
    }

    private void connectToNat() {
        nat_ctrl = new LocalSocket();
        for (int i = 0; i < 3; ++i) {
            try {
                nat_ctrl.connect(
                    new LocalSocketAddress(
                        app.natCtrlPath(),
                        LocalSocketAddress.Namespace.FILESYSTEM
                    )
                ); // NOTE: TIMEOUT IS NOT SUPPORTED!
                log(false, getString(R.string.filterok));
                return;
            } catch (java.io.IOException e) {
                Log.e(BarnacleApp.TAG, "LocalSocket.connect to '" + app.natCtrlPath() +
                          "' failed: " + e.toString());
            }
            try {
                Thread.sleep(100); // this is so wrong -- service should not halt
            } catch (InterruptedException e) {
                break;
            }
        }
        log(true, getString(R.string.filtererr));

        //nat_ctrl.close();
        nat_ctrl = null;
    }

    private boolean tellProcess(String msg) {
        if (process != null) {
            try {
                process.getOutputStream().write((msg+"\n").getBytes());
                return true;
            } catch (Exception e) {} // just ignore it
        }
        return false;
    }

    private boolean tellNat(String msg) {
        if (nat_ctrl != null) {
            try {
                DataOutputStream dos = new DataOutputStream(nat_ctrl.getOutputStream());
                assert msg.length() < 256;
                dos.writeByte(msg.length());
                dos.writeBytes(msg);
                return true;
            } catch (Exception e) {
                log(true, getString(R.string.filtererr));
                nat_ctrl = null;
            }
        }
        return false;
    }

    private void stopProcess() {
        if (process != null) {
            // first, just close the stream
            if (state != STATE_STOPPED) {
                try {
                    process.getOutputStream().close();
                } catch (Exception e) {
                    Log.w(BarnacleApp.TAG, "Exception while closing process");
                }
            }
            try {
                process.waitFor(); // blocking!
            } catch (InterruptedException e) {
                Log.e(BarnacleApp.TAG, "");
            }

            try {
                int exit_status = process.exitValue();
                Log.i(BarnacleApp.TAG, "Process exited with status: " + exit_status);
            } catch (IllegalThreadStateException e) {
                // this is not good
                log(true, getString(R.string.dirtystop));
            }
            process.destroy();
            process = null;
            threads[0].interrupt();
            threads[1].interrupt();
            nat_ctrl = null;
        }
        app.serviceStopped();
    }

    @Override
    public IBinder onBind(Intent intent) {
        // TODO add aidl for RPC
        return null;
    }
}

