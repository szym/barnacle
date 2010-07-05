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

import android.os.Bundle;
import android.view.View;
import android.view.ViewGroup;

import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.TextView;
import android.widget.BaseAdapter;

public class ClientsActivity extends android.app.ListActivity {
    private BarnacleApp app;
    private BaseAdapter adapter;

    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        app = (BarnacleApp)getApplication();
        app.setClientsActivity(this);

        adapter = new BaseAdapter(){
            public int getCount() { return app.service == null ? 0 : app.service.clients.size(); }
            public Object getItem(int position) { return app.service.clients.get(position); }
            public long getItemId(int position) { return position; }

            @Override
            public View getView(int position, View convertView, ViewGroup parent) {
                final BarnacleService.ClientData client = app.service.clients.get(position);

                View view = getLayoutInflater().inflate(R.layout.clientrow, null);
                TextView macaddress = (TextView) view.findViewById(R.id.macaddress);
                TextView ipaddress  = (TextView) view.findViewById(R.id.ipaddress);
                TextView hostname   = (TextView) view.findViewById(R.id.hostname);
                CheckBox allowed    = (CheckBox) view.findViewById(R.id.allowed);
                macaddress.setText(client.mac);
                ipaddress.setText(client.ip);
                hostname.setText(client.hostname != null ? client.hostname : "[ none ]");
                allowed.setChecked(client.allowed);
                if (app.service.hasFiltering()) { 
                    allowed.setVisibility(CheckBox.VISIBLE);
                    allowed.setOnCheckedChangeListener(new CheckBox.OnCheckedChangeListener() {
                        @Override
                        public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                            client.allowed = isChecked;
                            app.service.filterRequest(client.mac, client.allowed);
                        }
                    });
                } else {
                	allowed.setVisibility(CheckBox.INVISIBLE);
                }
                return view;
            }
        };
        setListAdapter(adapter);
        setTitle(getString(R.string.clientview));
    }
    @Override
    protected void onDestroy() {
        super.onDestroy();
        app.setClientsActivity(null);
    }

    @Override
    protected void onResume() {
        super.onResume();
        app.notificationManager.cancel(BarnacleApp.NOTIFY_CLIENT); // FIXME: hide me
        update();
    }

    public void update() {
        if (app.service == null || app.service.clients.size() == 0)
            app.updateToast("No clients connected", false);
        adapter.notifyDataSetChanged();
    }
}
