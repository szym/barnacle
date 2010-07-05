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
import android.preference.EditTextPreference;
import android.preference.ListPreference;
import android.preference.PreferenceActivity;
import android.preference.Preference;
import android.widget.Toast;

public class SettingsActivity extends PreferenceActivity implements Preference.OnPreferenceChangeListener {
    final static int[] prefids = {
        R.string.if_lan, R.string.if_wan, 
        R.string.lan_gw, R.string.lan_netmask, R.string.lan_essid, R.string.lan_bssid, R.string.lan_wep, R.string.lan_channel,
        R.string.dhcp_firsthost, R.string.dhcp_numhosts, R.string.dhcp_leasetime, R.string.dhcp_dns1, R.string.dhcp_dns2,
        R.string.nat_firstport, R.string.nat_numports, R.string.nat_queue, R.string.nat_timeout, R.string.nat_timeout_tcp, 
        R.string.lan_script
    };
    final static int[] checks = { R.string.nat_filter };

    private void setSummary(Preference p, CharSequence s) {
    	if ((s != null) && (s.length() > 0)) {
    		p.setSummary("Current: " + s);
    	} else {
    		p.setSummary(null);
    	}
    }
    
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        addPreferencesFromResource(R.xml.preferences);

        for (int i = 0; i < prefids.length; ++i) {
            Preference pref = findPreference(getString(prefids[i]));
            pref.setOnPreferenceChangeListener(this);
            if (ListPreference.class.isInstance(pref)) {
                ListPreference preference = (ListPreference) pref;
                setSummary(preference, preference.getValue());
            } else if (EditTextPreference.class.isInstance(pref)) {
                EditTextPreference preference = (EditTextPreference) pref;
                setSummary(preference, preference.getText());
            }
        }
        for (int i = 0; i < checks.length; ++i) {
        	Preference pref = findPreference(getString(checks[i]));
            pref.setOnPreferenceChangeListener(this);
        }
    }

    @Override
    public boolean onPreferenceChange(Preference pref, Object newValue) {
        String key = pref.getKey();
        if (key == null) return true;

        BarnacleService svc = ((BarnacleApp)getApplication()).service;
        if (svc != null && svc.isRunning()) {
            Toast.makeText(this, "Restart for changes to take effect", Toast.LENGTH_SHORT).show();
        }

        if (ListPreference.class.isInstance(pref) || EditTextPreference.class.isInstance(pref)) {
            setSummary(pref, (String)newValue);
        } // else don't update summary
        return true;
    }
}
