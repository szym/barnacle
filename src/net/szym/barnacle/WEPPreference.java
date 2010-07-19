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

import android.content.Context;
import android.content.DialogInterface;
import android.preference.DialogPreference;

import android.text.InputType;
import android.util.AttributeSet;
import android.view.View;
import android.text.method.DigitsKeyListener;
import android.text.method.TextKeyListener;
import android.widget.Toast;
import android.widget.AdapterView;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.Spinner;

public class WEPPreference extends DialogPreference implements
        AdapterView.OnItemSelectedListener, View.OnClickListener {

    final static int WEP_TYPE_ASCII = 0; // must match @array/wep_type
    final static int WEP_TYPE_HEX = 1;

    private String password; // as stored for the wpa_supplicant
    private int weptype;

    private CheckBox mEnabledCheckBox;
    private EditText mPasswordEdit;
    private CheckBox mShowPasswordCheckBox;
    private Spinner mWepTypeSpinner;

    public WEPPreference(Context context, AttributeSet attrs, int defStyle) {
        super(context, attrs, defStyle);
        setDialogLayoutResource(R.layout.wep);
    }

    public WEPPreference(Context context, AttributeSet attrs) {
        super(context, attrs);
        setDialogLayoutResource(R.layout.wep);
    }

    public static String getAsciiContent(String v) {
        if ((v.length() > 0) && (v.charAt(0) == '"')) {
            return v.substring(1, v.length() - 1);
        } else {
            return null;
        }
    }

    @Override
    protected void onBindDialogView(View view) {
        super.onBindDialogView(view);
        mEnabledCheckBox = (CheckBox) view
                .findViewById(R.id.wep_enabled_checkbox);
        mPasswordEdit = (EditText) view.findViewById(R.id.password_edit);
        mShowPasswordCheckBox = (CheckBox) view
                .findViewById(R.id.show_password_checkbox);
        mWepTypeSpinner = (Spinner) view.findViewById(R.id.wep_type_spinner);

        mWepTypeSpinner.setSelection(0); // something should be always selected

        if (password == null) password = "";

        boolean enabled = (password.length() != 0);
        // load values
        mEnabledCheckBox.setChecked(enabled);
        mPasswordEdit.setEnabled(enabled);
        mWepTypeSpinner.setEnabled(enabled);
        mShowPasswordCheckBox.setEnabled(enabled);

        if (enabled) {
            String pass = getAsciiContent(password);
            if (pass != null) {
                weptype = WEP_TYPE_ASCII;
                mPasswordEdit.setText(pass);
            } else {
                // hex stored directly
                weptype = WEP_TYPE_HEX;
                mPasswordEdit.setText(password);
            }
        }
        mWepTypeSpinner.setSelection(weptype);
        setWepType(weptype);
        mShowPasswordCheckBox.setChecked(false);
        setShowPassword(false);

        mEnabledCheckBox.setOnClickListener(this);
        mShowPasswordCheckBox.setOnClickListener(this);
        mWepTypeSpinner.setOnItemSelectedListener(this);
    }

    private void setWepType(int type) {
        if (type != weptype) {
            mPasswordEdit.setText(""); // must reset
            weptype = type;
        }
        if (type == WEP_TYPE_HEX) {
            mPasswordEdit.setKeyListener(DigitsKeyListener
                    .getInstance("0123456789abcdefABCDEF"));
            mPasswordEdit.setHint("10 or 26 hex digits");
        } else {
            mPasswordEdit.setKeyListener(TextKeyListener.getInstance());
            mPasswordEdit.setHint("5 or 13 characters");
        }
    }

    @Override
    public void onItemSelected(AdapterView<?> arg0, View arg1, int arg2,
            long arg3) {
        setWepType(mWepTypeSpinner.getSelectedItemPosition());
    }

    @Override
    public void onNothingSelected(AdapterView<?> arg0) {
        // we should never get here
    }

    @Override
    public void onClick(View v) {
        if (v == mEnabledCheckBox) {
            boolean enabled = mEnabledCheckBox.isChecked();
            mPasswordEdit.setEnabled(enabled);
            mWepTypeSpinner.setEnabled(enabled);
            mShowPasswordCheckBox.setEnabled(enabled);
            if (!enabled) {
                password = ""; // always resets the password
            }
        } else if (v == mShowPasswordCheckBox) {
            setShowPassword(mShowPasswordCheckBox.isChecked());
        }
    }

    private void setShowPassword(boolean showPassword) {
        mPasswordEdit.setInputType(InputType.TYPE_CLASS_TEXT
                        | (showPassword ? InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD
                                        : InputType.TYPE_TEXT_VARIATION_PASSWORD));
    }

    @Override
    protected void onDialogClosed(boolean positiveResult) {
        if (positiveResult) {
            if (!callChangeListener(password)) return;
            // commit
            persistString(password); 
            notifyChanged();
        }
    }
    
    private boolean validateAndCommit() {
        if (!mEnabledCheckBox.isChecked()) {
            password = "";
            return true;
        }
        String pass = mPasswordEdit.getText().toString();
        int type = mWepTypeSpinner.getSelectedItemPosition();
        if (type == WEP_TYPE_ASCII) {
            if (pass.length() != 5 && pass.length() != 13) {
                Toast.makeText(getContext(), "WEP key must have 5 or 13 characters", Toast.LENGTH_LONG).show();
                return false;
            }
            password = '"' + pass + '"';
        } else {
            if (pass.length() != 10 && pass.length() != 26) {
                Toast.makeText(getContext(), "WEP key must have 10 or 26 hex digits", Toast.LENGTH_LONG).show();
                return false;
            }
            password = pass;
        }
        return true;
    }

    @Override
    public void onClick(DialogInterface dialog, int which) {
        if (which == DialogInterface.BUTTON_POSITIVE) {
            if (!validateAndCommit()) {
                return; // not done yet
            }
        } 
        super.onClick(dialog, which);
    }

    @Override
    protected void onSetInitialValue(boolean restoreValue, Object defaultValue) {
        if (restoreValue) {
            password = getPersistedString(password);
        } else {
            password = (String) defaultValue;
            persistString(password);
        }
    }

}
