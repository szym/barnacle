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

import java.text.NumberFormat;

import android.app.AlertDialog;
import android.app.Dialog;
import android.content.DialogInterface;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.view.Window;
import android.view.View.OnClickListener;
import android.widget.TabHost;
import android.widget.TextView;
import android.widget.ToggleButton;
import android.widget.Button;

public class StatusActivity extends android.app.TabActivity {
	private BarnacleApp app;

	private TabHost tabs;
    private ToggleButton onoff;
    private Button announce;
    private TextView logview;
    private boolean paused;

    final static int DLG_ABOUT = 0;
    final static int DLG_ROOT = 1;
    final static int DLG_ERROR = 2;

    static NumberFormat nf = NumberFormat.getInstance();
    static {
        nf.setMaximumFractionDigits(2);
        nf.setMinimumFractionDigits(2);
        nf.setMinimumIntegerDigits(1);
    }

    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        
        app = (BarnacleApp)getApplication();
        
        requestWindowFeature(Window.FEATURE_INDETERMINATE_PROGRESS);
        setProgressBarIndeterminate(true);
        setContentView(R.layout.main);

        // control interface
        onoff = (ToggleButton) findViewById(R.id.onoff);
        onoff.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View v) {
                onoff.setPressed(true);
                if(onoff.isChecked()) app.service.startRequest();
                else app.service.stopRequest();
            }
        });
        announce = (Button) findViewById(R.id.announce);
        announce.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View v) {
                app.service.assocRequest();
            }
        });
        
        tabs = getTabHost();
        tabs.addTab(tabs.newTabSpec("log")
        		.setIndicator("log", getResources().getDrawable(R.drawable.ic_tab_recent))
        		.setContent(R.id.logview));
        tabs.addTab(tabs.newTabSpec("traffic")
        		.setIndicator("traffic", getResources().getDrawable(R.drawable.ic_tab_starred))
        		.setContent(R.id.traffic));
        tabs.addTab(tabs.newTabSpec("clients")
                .setIndicator("clients", getResources().getDrawable(R.drawable.ic_tab_contacts))
                .setContent(new Intent(this, ClientsActivity.class)));
        
        logview = (TextView) findViewById(R.id.log_text);
        app.setStatusActivity(this);
        paused = false;
        onoff.setEnabled(false);
        announce.setEnabled(false);
    }
    @Override
    protected void onDestroy() {
        super.onDestroy();
        app.setStatusActivity(null);
    }
    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        getMenuInflater().inflate(R.menu.main, menu);
        return true;
    }
    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        switch (item.getItemId()) {
        case R.id.menu_prefs:
            startActivity(new Intent(this, SettingsActivity.class));
            return true;
        case R.id.menu_about:
            showDialog(DLG_ABOUT);
            return true;
        }
        return(super.onOptionsItemSelected(item));
    }
    @Override
    protected Dialog onCreateDialog(int id) {
        if (id == DLG_ABOUT) {
            return (new AlertDialog.Builder(this))
            	.setIcon(android.R.drawable.ic_dialog_info)
                .setTitle("Help")
                .setMessage("Barnacle was developed by szym.net. Donations are welcome."
                            +"\n\n"
                            +"Press the 'Associate' button to re-announce the ad-hoc network. "
                            +"See Website for help and more info.")
                .setPositiveButton("Donate", new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface dialog, int which) {
                        Uri uri = Uri.parse(getString(R.string.paypalUrl));
                        startActivity(new Intent(Intent.ACTION_VIEW, uri));
                    }})
                .setNeutralButton("Website", new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface dialog, int which) {
                        Uri uri = Uri.parse(getString(R.string.websiteUrl));
                        startActivity(new Intent(Intent.ACTION_VIEW, uri));
                    }})
                .setNegativeButton("OK", new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface dialog, int which) { removeDialog(DLG_ABOUT); }}) // just close
                .create();
        }
        if (id == DLG_ROOT) {
            return (new AlertDialog.Builder(this))
            	.setIcon(android.R.drawable.ic_dialog_alert)
                .setTitle("Root Access")
                .setMessage("Barnacle requires 'su' to access the hardware! Please, make sure you have root access.")
                .setPositiveButton("Help", new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface dialog, int which) {
                        Uri uri = Uri.parse(getString(R.string.rootUrl));
                        startActivity(new Intent(Intent.ACTION_VIEW, uri));
                    }})
                .setNegativeButton("Close", new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface dialog, int which) { removeDialog(DLG_ROOT); }})
                .create();
        }
        if (id == DLG_ERROR) {
            return (new AlertDialog.Builder(this))
            	.setIcon(android.R.drawable.ic_dialog_alert)
                .setTitle("Error")
                .setMessage("Unexpected error occured! Inspect the log and try the troubleshooting guide.")
                .setPositiveButton("Help", new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface dialog, int which) {
                        Uri uri = Uri.parse(getString(R.string.fixUrl));
                        startActivity(new Intent(Intent.ACTION_VIEW, uri));
                    }})
                .setNegativeButton("Close", new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface dialog, int which) { removeDialog(DLG_ERROR); }})
                .create();
        }
        return null;
    }

    @Override
    protected void onPause() {
        super.onPause();
        paused = true;
    }
    @Override
    protected void onResume() {
        super.onResume();
        paused = false;
        app.ensureService();
        if (BarnacleApp.ACTION_CLIENTS.equals(getIntent().getAction())) {
            getTabHost().setCurrentTab(2); // show clients
        }
        update();
        if (app.service != null && !app.service.isRunning())
        	app.serviceStopped(); // clean up notifications
    }
	@Override
	protected void onNewIntent(Intent intent) {
		super.onNewIntent(intent);
		if (BarnacleApp.ACTION_CLIENTS.equals(intent.getAction())) {
            getTabHost().setCurrentTab(2); // show clients
        }
	}

    static String format(long v) {
        if (v < 1000000) return nf.format(v /    1000.0f) + " kB";
        else             return nf.format(v / 1000000.0f) + " MB";
    }

    void update() {
        if (paused) return; // no need to update
        BarnacleService svc = app.service;
        if (svc == null) return; // not ready yet!
        onoff.setEnabled(true);
        setProgressBarIndeterminateVisibility(svc.isChanging());
        announce.setEnabled(svc.isRunning());
        logview.setText(svc.log);
        if (svc.isChanging()) {
            onoff.setPressed(true);
            return;
        }
        onoff.setPressed(false);
        onoff.setChecked(svc.isRunning());
        Util.TrafficStats stats = svc.stats;
        ((TextView)findViewById(R.id.download)).setText(format(stats.total.tx_bytes));
        ((TextView)findViewById(R.id.upload  )).setText(format(stats.total.rx_bytes));
        ((TextView)findViewById(R.id.download_rate)).setText(format(stats.rate.tx_bytes)+"/s");
        ((TextView)findViewById(R.id.upload_rate  )).setText(format(stats.rate.rx_bytes)+"/s");
        svc.statsRequest(1000);
    }
}
