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

import android.os.Bundle;
import android.view.View;
import android.view.ViewGroup;

import android.widget.AdapterView;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.TextView;
import android.widget.BaseAdapter;
import android.widget.AdapterView.OnItemClickListener;
import android.widget.AdapterView.OnItemLongClickListener;

public class ClientsActivity extends android.app.ListActivity {
    private BarnacleApp app;
    private BaseAdapter adapter;
    private ArrayList<BarnacleService.ClientData> clients = new ArrayList<BarnacleService.ClientData>();

    private static class ViewHolder {
        TextView macaddress;
        TextView ipaddress;
        TextView hostname;
        CheckBox allowed;
    }

    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        app = (BarnacleApp)getApplication();
        app.setClientsActivity(this);

        adapter = new BaseAdapter(){
            public int getCount() { return clients.size(); }
            public Object getItem(int position) { return clients.get(position); }
            public long getItemId(int position) { return position; }

            @Override
            public View getView(int position, View convertView, ViewGroup parent) {
                final BarnacleService.ClientData client = clients.get(position);

                ViewHolder holder;

                if (convertView == null) {
                    View view = getLayoutInflater().inflate(R.layout.clientrow, null);
                    holder = new ViewHolder();
                    holder.macaddress = (TextView) view.findViewById(R.id.macaddress);
                    holder.ipaddress  = (TextView) view.findViewById(R.id.ipaddress);
                    holder.hostname   = (TextView) view.findViewById(R.id.hostname);
                    holder.allowed    = (CheckBox) view.findViewById(R.id.allowed);
                    view.setTag(holder);
                    convertView = view;
                } else {
                    holder = (ViewHolder) convertView.getTag();
                }

                holder.macaddress.setText(client.mac);
                holder.ipaddress.setText(client.ip);
                holder.hostname.setText(client.hostname != null ? client.hostname : "[ none ]");
                holder.allowed.setChecked(client.allowed);
                if (app.service != null && app.service.hasFiltering()) {
                    holder.allowed.setVisibility(CheckBox.VISIBLE);
                    holder.allowed.setOnCheckedChangeListener(new CheckBox.OnCheckedChangeListener() {
                        @Override
                        public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                            client.allowed = isChecked;
                            if (app.service != null) {
                                app.service.filterRequest(client.mac, client.allowed);
                            } else {
                                buttonView.setVisibility(View.INVISIBLE); // is it not confusing?
                            }
                        }
                    });
                } else {
                    holder.allowed.setVisibility(View.INVISIBLE);
                }
                return convertView;
            }
        };
        setListAdapter(adapter);
        setTitle(getString(R.string.clientview));
        getListView().setLongClickable(true);
        getListView().setOnItemLongClickListener(new OnItemLongClickListener() {
            @Override
            public boolean onItemLongClick(AdapterView<?> arg0, View v,
                    int position, long id) {
                app.dmzRequest(clients.get(position).ip);
                return true;
            }
        });
        getListView().setOnItemClickListener(new OnItemClickListener() {
            @Override
            public void onItemClick(AdapterView<?> arg0, View v, int arg2,
                    long arg3) {
                ViewHolder holder = (ViewHolder)v.getTag();
                holder.allowed.performClick();
            }
        });
    }
    @Override
    protected void onDestroy() {
        super.onDestroy();
        app.setClientsActivity(null);
    }

    @Override
    protected void onResume() {
        super.onResume();
        app.cancelClientNotify();
        update();

        if (hasWindowFocus() && clients.isEmpty())
            app.updateToast(getString(R.string.noclients), false);
    }

    public void update() {
        if (app.service != null)
            clients = app.service.clients;
        adapter.notifyDataSetChanged();
    }
}
