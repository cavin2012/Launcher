package org.libsdl.app;

import java.io.IOException;
import java.io.InputStream;
import java.text.SimpleDateFormat;
import java.util.Arrays;
import java.lang.reflect.Method;
import java.io.File;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.Date;
import java.util.Iterator;
import java.util.List;
import java.util.TimeZone;
import java.util.UUID;

import android.Manifest;
import android.app.admin.DevicePolicyManager;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattServer;
import android.bluetooth.BluetoothGattServerCallback;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.le.AdvertiseCallback;
import android.bluetooth.le.AdvertiseData;
import android.bluetooth.le.AdvertiseSettings;
import android.bluetooth.le.BluetoothLeAdvertiser;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanFilter;
import android.bluetooth.le.ScanRecord;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.res.AssetManager;
import android.content.res.Resources;
import android.database.Cursor;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbManager;
import android.media.AudioRecord;
import android.net.Uri;
import android.provider.MediaStore;
import android.provider.Settings;
import android.support.annotation.NonNull;
import android.support.v4.app.ActivityCompat;
import android.support.v4.content.ContextCompat;
import android.support.v4.view.ViewCompat;

import android.app.*;
import android.content.*;
import android.content.res.Configuration;
import android.text.InputType;
import android.text.TextUtils;
import android.view.*;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;
import android.widget.RelativeLayout;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.os.*;
import android.util.DisplayMetrics;
import android.util.Log;
import android.util.SparseArray;
import android.graphics.*;
import android.graphics.drawable.Drawable;
import android.hardware.*;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.content.pm.ApplicationInfo;

class SortPictureList implements Comparator {
    @Override
    public int compare(Object o, Object t1) {
        String path1 = o.toString();
        String path2 = t1.toString();
        File file1 = new File(path1);
        File file2 = new File(path2);
        if (file1.lastModified() < file2.lastModified()) {
            return 1;
        } else return -1;
    }
}

/**
    SDL Activity
*/
public class SDLActivity extends Activity implements View.OnSystemUiVisibilityChangeListener {
    private static final String TAG = "SDL";
    private static int MY_PERMISSIONS_REQUEST = 100;
    protected static ArrayList<String> mPermissionList = new ArrayList<String>();
    protected static boolean mForegroundService = false;

    public static boolean mIsResumedCalled, mIsSurfaceReady, mHasFocus;

    // Handle the state of the native layer
    public enum NativeState {
           INIT, RESUMED, PAUSED
    }

    public static NativeState mNextNativeState;
    public static NativeState mCurrentNativeState;

    public static boolean mExitCalledFromJava;

    /** If shared libraries (e.g. SDL or the native application) could not be loaded. */
    public static boolean mBrokenLibraries;

    // If we want to separate mouse and touch events.
    //  This is only toggled in native code when a hint is set!
    public static boolean mSeparateMouseAndTouch;

    // Main components
    protected static SDLActivity mSingleton;
    protected static SDLSurface mSurface;
    protected static View mTextEdit;
    protected static boolean mScreenKeyboardShown;
    protected static ViewGroup mLayout;
    protected static SDLClipboardHandler mClipboardHandler;


    // This is what SDL runs in. It invokes SDL_main(), eventually
    protected static Thread mSDLThread = null;

    // Audio
    private static SDLService mPlaybackAudioService = null;
    protected static AudioRecord mAudioRecord;

    // Ble
    protected static BluetoothManager mBluetoothManager = null;
    protected static BluetoothAdapter mBluetoothAdapter = null;
    protected static BluetoothLeScanner mLEScanner;

    public static boolean mOrientationResult = false;
    public static boolean mScreenOn = true;

    // DevicePolicyManager
    private int RC_POLICYMANAGER = 1;
    private ComponentName mAdminReceiver;
    private DevicePolicyManager mPolicyManager;
    private static PowerManager mPowerManager;

    /**
     * This method returns the name of the shared object with the application entry point
     * It can be overridden by derived classes.
     */
    protected String getMainSharedObject() {
        String library;
        String[] libraries = SDLActivity.mSingleton.getLibraries();
        if (libraries.length > 0) {
            library = "lib" + libraries[libraries.length - 1] + ".so";
        } else {
            library = "libmain.so";
        }
        return library;
    }

    /**
     * This method returns the name of the application entry point
     * It can be overridden by derived classes.
     */
    protected String getMainFunction() {
        return "SDL_main";
    }

    /**
     * This method is called by SDL before loading the native shared libraries.
     * It can be overridden to provide names of shared libraries to be loaded.
     * The default implementation returns the defaults. It never returns null.
     * An array returned by a new implementation must at least contain "SDL2".
     * Also keep in mind that the order the libraries are loaded may matter.
     * @return names of shared libraries to be loaded (e.g. "SDL2", "main").
     */
    protected String[] getLibraries() {
        return new String[] {
            "SDL2",
            "SDL2_image",
            "SDL2_mixer",
            "SDL2_ttf",
            "c++_shared",
            "vpx",
            "main"
        };
    }

    // Load the .so
    public void loadLibraries() {
       for (String lib : getLibraries()) {
          System.loadLibrary(lib);
       }
    }

    /**
     * This method is called by SDL before starting the native application thread.
     * It can be overridden to provide the arguments after the application name.
     * The default implementation returns an empty array. It never returns null.
     * @return arguments for the native application.
     */
    protected String[] getArguments() {
        return new String[0];
    }

    public static void initialize() {
        // The static nature of the singleton and Android quirkyness force us to initialize everything here
        // Otherwise, when exiting the app and returning to it, these variables *keep* their pre exit values
        mSingleton = null;
        mSurface = null;
        mTextEdit = null;
        mLayout = null;
        mClipboardHandler = null;
        // mSDLThread = null;
        mExitCalledFromJava = false;
        mBrokenLibraries = false;
        mIsResumedCalled = false;
        mIsSurfaceReady = false;
        mHasFocus = true;
        mNextNativeState = NativeState.INIT;
        mCurrentNativeState = NativeState.INIT;
    }

    private void activeDevicePolicyManager() {
        mAdminReceiver = new ComponentName(getContext(), ScreenOffAdminReceiver.class);
        mPolicyManager = (DevicePolicyManager)getContext().getSystemService(Context.DEVICE_POLICY_SERVICE);

        Intent intent = new Intent(DevicePolicyManager.ACTION_ADD_DEVICE_ADMIN);
        intent.putExtra(DevicePolicyManager.EXTRA_DEVICE_ADMIN, mAdminReceiver);
        intent.putExtra(DevicePolicyManager.EXTRA_ADD_EXPLANATION,"开启后就可以使用锁屏功能了...");//显示位置见图二

        startActivityForResult(intent, RC_POLICYMANAGER);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == RC_POLICYMANAGER) {
            if (!mPolicyManager.isAdminActive(mAdminReceiver)) {
                // User has inactived DevicePolicyManager, require user active it.
                final AlertDialog.Builder dlgAlert  = new AlertDialog.Builder(this);
                dlgAlert.setMessage("未激活设备管理器，你必须选择激活此设备管理员"
                        + System.getProperty("line.separator")
                        + System.getProperty("line.separator")
                        + "单击\"退出\"，然后重新运行App，选择激活此设备管理员");
                dlgAlert.setTitle("激活设备管理器");
                dlgAlert.setPositiveButton("退出",
                        new DialogInterface.OnClickListener() {
                            @Override
                            public void onClick(DialogInterface dialog,int id) {
                                // if this button is clicked, close current activity
                                SDLActivity.mSingleton.finish();
                            }
                        });
                dlgAlert.setCancelable(false);
                dlgAlert.create().show();
            }
        } else if (requestCode == REQUEST_ENABLE_BT && resultCode == Activity.RESULT_OK){
            Log.d(TAG, "Turning on Bluetooth was successful.");
        }
    }

    public static void enableStatusBar(Context context, boolean enable) {
    }

    public static void enableRelay(boolean enable) {
        {
            if (enable) {
                Intent intent1 = new Intent("com.android.intent.OpenRelay");
                SDLActivity.mSingleton.sendBroadcast(intent1);
            } else {
                Intent intent1 = new Intent("com.android.intent.CloseRelay");
                SDLActivity.mSingleton.sendBroadcast(intent1);
            }
        }
    }

    public enum LightColor {
        LColorNone, LColorWhite, LColorRed, LColorGreen, LColorBlue, LColorBlack
    }
    public static void turnOnFlashlight(int nColor) {
        LightColor color = LightColor.values()[nColor];
        {
            if (color != LightColor.LColorNone) {
                Intent intent1 = new Intent("com.android.intent.OpenFlashlight");
                SDLActivity.mSingleton.sendBroadcast(intent1);
            } else {
                Intent intent1 = new Intent("com.android.intent.CloseFlashlight");
                SDLActivity.mSingleton.sendBroadcast(intent1);
            }
        }
    }

    public static void turnOnScreen(boolean on) {
        if (on) {
            Log.i(TAG, "SDLActivity, turnOffScreen, step1");
            // 获取PowerManager.WakeLock对象,后面的参数|表示同时传入两个值,最后的是LogCat里用的Tag
            PowerManager.WakeLock wl = mPowerManager.newWakeLock(PowerManager.ACQUIRE_CAUSES_WAKEUP | PowerManager.SCREEN_BRIGHT_WAKE_LOCK, "bright");
            // must not use below statemant.
            // PowerManager.WakeLock wl = mPowerManager.newWakeLock(PowerManager.ACQUIRE_CAUSES_WAKEUP, mSingleton.getClass().getName());
            wl.acquire(10000); // 点亮屏幕
            wl.release(); // 释放
/*
            // 屏幕解锁
            KeyguardManager keyguardManager = (KeyguardManager)SDL.getContext().getSystemService(KEYGUARD_SERVICE);
            KeyguardManager.KeyguardLock keyguardLock = keyguardManager.newKeyguardLock("unLock");
            // 屏幕锁定
            keyguardLock.reenableKeyguard();
            keyguardLock.disableKeyguard(); // 解锁
*/
        } else {
            Log.i(TAG, "SDLActivity, turnOffScreen");
            if (mSingleton.mPolicyManager == null) {
                return;
            }
            boolean admin = mSingleton.mPolicyManager.isAdminActive(mSingleton.mAdminReceiver);
            if (admin) {
                mSingleton.mPolicyManager.lockNow();
            }
        }
    }

    public static void setBrightness(int _brightness) {
        int brightness = _brightness < 0? 0: _brightness;
        brightness = brightness > 255? 255: brightness;
    }

    public static int translateKeycode(int keyCode) {
        return keyCode;
    }

    // return value
    // 0: asset_type_none
    // 1: asset_type_directory
    // 2: asset_type_file
    public static int getAssetType(Context context, String id, boolean directory) {
        AssetManager manager = context.getAssets();

        try {
            InputStream in = null;
            if (directory) {
                String id2 = id + "/__cookie.cki";
                in = manager.open(id2);
            } else {
                in = manager.open(id);
            }
            if (in == null) {
                return 0;
            }

            in.close();
            return directory? 1: 2;

        } catch (IOException e) {
            // e.printStackTrace();
            // System.err.println(e.getMessage());
            return 0;
        }
    }

    public static List<String> opendir(Context context, String id) {
        AssetManager manager = context.getAssets();

        List<String> result = new ArrayList<String>();
        try {
            int length = 0;
            String fileNames[] = context.getAssets().list(id);
            for (String fileName : fileNames) {
                length += fileName.length() + 1;
            }
            result.add(Integer.toString(length));
            for (String fileName : fileNames) {
                result.add(fileName);
            }

            return result;

        } catch (IOException e) {
            // e.printStackTrace();
            // System.err.println(e.getMessage());
            result = new ArrayList<String>();
            return result;
        }
    }

    public static String openphoto(Context mContext) {
        String dcim_path = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DCIM).getAbsolutePath();
        List<String> list = new ArrayList<>();

        String ret = "";
        int count = 0;

        Uri pictureUri = MediaStore.Images.Media.EXTERNAL_CONTENT_URI;
        ContentResolver contentResolver = mContext.getContentResolver();
        // filter: jpeg, png
        Cursor cursor = contentResolver.query(pictureUri, null,
                MediaStore.Images.Media.MIME_TYPE + "=? or "
                        + MediaStore.Images.Media.MIME_TYPE + "=?",
                new String[]{"image/jpeg", "image/png"}, MediaStore.Images.Media.DATE_MODIFIED);
        //    new String[]{"image/png"}, MediaStore.Images.Media.DATE_MODIFIED);
        if (cursor == null || cursor.getCount() == 0) {
            // mHandler.sendEmptyMessage(PickPictureHandler.SCAN_ERROR);
        } else {
            while (cursor.moveToNext()) {
                String path = cursor.getString(cursor.getColumnIndex(MediaStore.Images.Media.DATA));
                if (!path.startsWith(dcim_path)) {
                    continue;
                }
                list.add(path);

            }
            cursor.close();

            SortPictureList sortList = new SortPictureList();
            Collections.sort(list, sortList);

            count = list.size();
        }

        ret = Integer.toString(count);
        ret += '|';
        for (String file: list) {
            ret += file + '|';
        }
        // retval of String.length() is characters, not bytes.
        return ret;
    }

    public static int getDpiScale(Context context) {
        DisplayMetrics metrics = context.getResources().getDisplayMetrics();
        int hdpi_scale = (int)metrics.scaledDensity;
        if (hdpi_scale == 1) {
            if (metrics.scaledDensity - hdpi_scale >= 0.1) {
                hdpi_scale += 1;
            }
        } else if (metrics.scaledDensity - hdpi_scale >= 0.4) {
            hdpi_scale += 1;
        }
        if (hdpi_scale < 2) {
            // I think, all android's hdpi must be >= 2.
            hdpi_scale = 2;
        }
        Log.v(TAG, "metrics.density: " + metrics.density + ", xdpi: " + metrics.xdpi + ", ydpi: " + metrics.xdpi + ", hdpi_scale: " + hdpi_scale);
        return hdpi_scale;
    }

    public static void setProperty(String key, String value) {
        try {
            Class<?> c = Class.forName("android.os.SystemProperties");
            Method set = c.getMethod("set", String.class, String.class);
            set.invoke(c, key, value );
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    public static void updateApp(String apk) {
        Context context = mSingleton.getApplicationContext();
        Intent mIntent = new Intent(context, UpdateService.class);
        mIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        mIntent.putExtra("apkname", apk);
        // start service
        context.startService(mIntent);
    }

    public class OsInfo {
        public String display;
        public String model;
        public String manufacturer;
    }

    public static List<OsInfo> getOsInfo() {
        List<OsInfo> result = new ArrayList<OsInfo>();

        OsInfo ret = mSingleton.new OsInfo();
        ret.manufacturer = Build.MANUFACTURER;
        ret.display = android.os.Build.DISPLAY;
        ret.model = android.os.Build.MODEL;
        result.add(ret);

        return result;
    }

    public static List<String> readcard(Context context) {
        List<String> result = new ArrayList<String>();
        return result;
    }

    public static boolean printText(byte[] txt_msg) {
        return true;
    }

    public static void reboot() {
        {
            setProperty("ctl.start", "boot_linux");
        }
    }

    public static void setAppType(int nType) {
    }

    private static final String YMDHMS_FORMAT = "yyyy-MM-dd HH:mm:ss";
    public static String longUtcTime2String(long longNum, String dateFormat, boolean utc) {
        if (TextUtils.isEmpty(dateFormat)) {
            dateFormat = YMDHMS_FORMAT;
        }
        SimpleDateFormat format = new SimpleDateFormat(dateFormat);
        if (utc) {
            format.setTimeZone(TimeZone.getTimeZone("UTC"));
        }
        Date date = new Date(longNum * 1000);
        return format.format(date);
    }

    public static boolean setTime(Context context, long utcTime) {
        {
            // https://kanru.info/blog/archives/861/
            // Android framework can execute some predefined command (service) via the "ctl.start" property,
            // the most interesting part is you can supply dynamic arguments to the command just like you were calling a command from shell.
            // The format is: setprop ctl.start service:arg1 arg2 arg3..
            StringBuilder sb = new StringBuilder("system_setdate:");
            sb.append(longUtcTime2String(utcTime, "yyyyMMdd.HHmmss", false));
            setProperty("ctl.start", sb.toString());
        }
        return true;
    }

    public static String createGUID(boolean line) {
        String identity = java.util.UUID.randomUUID().toString().toLowerCase();
        if (line == false) {
            identity = identity.replace("-", "");
        }
        return identity;
    }

    private final ServiceConnection mPlaybackAudioServiceConnection = new ServiceConnection() {

        @Override
        public void onServiceConnected(ComponentName componentName,IBinder service) {
            mPlaybackAudioService = ((SDLService.LocalBinder) service).getService();
            Log.i(TAG, "SDLService, mPlaybackAudioServiceConnection, onServiceConnected, " + System.currentTimeMillis());
        }


        @Override
        public void onServiceDisconnected(ComponentName componentName) {
            mPlaybackAudioService = null;
            Log.i(TAG, "SDLService, mPlaybackAudioServiceConnection, onServiceDisconnected, " + System.currentTimeMillis());
        }
    };

    public static native void nativeDiscoverPeripheral(BluetoothDevice device, int rssi, byte[] manufacturerData);
    public static native void nativeConnectionStateChange(BluetoothGatt gatt, boolean connected);
    public static native void nativeServicesDiscovered(BluetoothGatt gatt, boolean success);
    public static native void nativeCharacteristicRead(BluetoothGattCharacteristic chara, byte[] buffer, boolean notify);
    public static native void nativeCharacteristicWrite(BluetoothGattCharacteristic chara, boolean success);
    public static native void nativeDescriptorWrite(BluetoothGattDescriptor descriptor);

    // Implements callback methods for GATT events that the app cares about.  For example,
    // connection change and services discovered.
    public static BluetoothGattCallback mGattCallback = new BluetoothGattCallback() {

        @Override
        public void onConnectionStateChange(BluetoothGatt gatt, int status, int newState) {
            // String address = gatt.getDevice().getAddress();
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                Log.i(TAG, "Connected to GATT server.");
                nativeConnectionStateChange(gatt, true);

            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                Log.i(TAG, "Disconnected from GATT server.");
                nativeConnectionStateChange(gatt, false);
            }
        }

        @Override
        public void onServicesDiscovered(BluetoothGatt gatt, int status) {
            Log.i(TAG, "onServiceDiscovered: status: " + status + ", service count: " + gatt.getServices().size());
            nativeServicesDiscovered(gatt, status == BluetoothGatt.GATT_SUCCESS);
        }

        @Override
        public void onCharacteristicRead(BluetoothGatt gatt,
                                         BluetoothGattCharacteristic characteristic,
                                         int status) {
            // Log.i(TAG, "onCharacteristicRead: uuid " + characteristic.getUuid().toString() + ", status: " + status);
            nativeCharacteristicRead(characteristic, status == BluetoothGatt.GATT_SUCCESS? characteristic.getValue(): null, false);
        }

        @Override
        public void onCharacteristicWrite(BluetoothGatt gatt,
                                          BluetoothGattCharacteristic characteristic, int status) {
            // Log.i(TAG, "onCharacteristicWrite: uuid " + characteristic.getUuid().toString() + ", status: " + status);
            nativeCharacteristicWrite(characteristic, status == BluetoothGatt.GATT_SUCCESS);
        }

        @Override
        public void onCharacteristicChanged(BluetoothGatt gatt,
                                            BluetoothGattCharacteristic characteristic) {
            // Log.i(TAG, "onCharacteristicChanged: uuid " + characteristic.getUuid().toString());
            nativeCharacteristicRead(characteristic, characteristic.getValue(), true);
        }

        @Override
        public void onDescriptorRead(BluetoothGatt gatt, BluetoothGattDescriptor descriptor,
                                     int status) {
            Log.i(TAG, "onDescriptorRead: uuid " + descriptor.getUuid().toString() + ", status: " + status);
        }

        @Override
        public void onDescriptorWrite(BluetoothGatt gatt, BluetoothGattDescriptor descriptor,
                                      int status) {
            Log.i(TAG, "onDescriptorWrite: uuid " + descriptor.getUuid().toString() + ", status: " + status);
            nativeDescriptorWrite(descriptor);
        }
    };

    // Device scan callback.
    private static ScanCallback mScanCallback = null;

    private static BluetoothAdapter.LeScanCallback mLeScanCallback =
            new BluetoothAdapter.LeScanCallback() {

                @Override
                public void onLeScan(final BluetoothDevice device, final int rssi, byte[] scanRecord) {
                    // it isn't in main thread. to safe, should switch to main
                    // Log.i(TAG, "address: " + device.getAddress() + " name: " + device.getName());
                    nativeDiscoverPeripheral(device, rssi, null);
                }
            };

    private void bleInitManager()
    {
        if (!getPackageManager().hasSystemFeature(PackageManager.FEATURE_BLUETOOTH_LE)) {
            // toast("设备不支持BLE");
            finish();
        }
        mBluetoothManager = (BluetoothManager)getSystemService(Context.BLUETOOTH_SERVICE);
        if (mBluetoothManager == null) {
            Log.e(TAG, "Unable to initialize BluetoothManager.");
            return;
        } else {
            mBluetoothAdapter = mBluetoothManager.getAdapter();
        }

        if (mBluetoothAdapter == null) {
            Log.e(TAG, "Unable to obtain a BluetoothAdapter.");
            return;
        }

        if (!mBluetoothAdapter.isEnabled()) {
            Log.e(TAG, "Current don't enable bluetooth, try to enable...");
            // "Settings" --> "Bluetooth" --> Off(wan to On)
            //  蓝牙正处于“关闭”状态，开启有两种方法，第一种是代码自动打开，第二种弹出一种提示框，叫用户选择是否要打开。
            boolean sliceTurnon = true;
            if (sliceTurnon) {
                boolean success = mBluetoothAdapter.enable();
                Log.e(TAG, "Enable bluetooth adapter, result: " + (success ? "true" : "false"));
                if (!success) {
                    return;
                }
            } else {
                //  以下代码请求“开启”，这会弹出一个提示框
                //   某个应用想要开启此设备的蓝牙功能。
                //                   拒绝    允许
                // 选择允许，程序会自动去开启，等开启成功后，app会收到requestCode == REQUEST_ENABLE_BT的onActivityResult
                // 目前peripherl只用于Launcher,launcher要求无人值守，没人会去单击按钮，为此注释掉以下代码。
                Intent enableBtIntent = new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE);
                startActivityForResult(enableBtIntent, REQUEST_ENABLE_BT);
            }
        }

        if (Build.VERSION.SDK_INT >= 21) {
            if (Build.VERSION.SDK_INT >= 23) {
                if (mSingleton.checkSelfPermission(Manifest.permission.ACCESS_COARSE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
                    int yourPermissionRequestCode = 1;
                    mSingleton.requestPermissions(new String[]{Manifest.permission.ACCESS_COARSE_LOCATION}, yourPermissionRequestCode);
                }
            }
        }
        Log.d(TAG, "Initialize bleInitManager success.");
    }

    private static boolean bleCenterInitialized = false;

    public static BluetoothAdapter bleInitCenter(Context context)
    {
        if (bleCenterInitialized) {
            return mBluetoothAdapter;
        }
        bleCenterInitialized = true;

        if (mBluetoothManager == null) {
            SDLActivity.mSingleton.bleInitManager();
        }
        if (mBluetoothAdapter == null) {
            return null;
        }

        if (Build.VERSION.SDK_INT >= 21) {
            mLEScanner = mBluetoothAdapter.getBluetoothLeScanner();

            mScanCallback = new ScanCallback() {
                @Override
                public void onScanResult(int callbackType, ScanResult result) {
                    Log.i("callbackType", String.valueOf(callbackType));
                    Log.i("result", result.toString());
                    BluetoothDevice device = result.getDevice();
                    // id: 0x12 0x13, data: 0x01 0x02 0x03 ==> manufacturerData: 0x12 0x13 0x01 0x02 0x03
                    // 如果存在多个manufacturerData, 只返回最后一个.
                    byte[] manufacturerData = null;
                    ScanRecord record = result.getScanRecord();
                    SparseArray<byte[]> dataArray = record.getManufacturerSpecificData();
                    for (int i = 0; dataArray != null && i < dataArray.size(); i ++) {
                        int key = dataArray.keyAt(i);
                        byte[] user = dataArray.get(key);
                        manufacturerData = new byte[2 + (user != null? user.length: 0)];
                        manufacturerData[0] = ((byte)(key & 0xFF));
                        manufacturerData[1] = ((byte)(key >> 8 & 0xFF));
                        for (int at = 0; user != null && at < user.length; at ++) {
                            manufacturerData[2 + at] = user[at];
                        }
                    }
                    nativeDiscoverPeripheral(device, result.getRssi(), manufacturerData);
                }

                @Override
                public void onBatchScanResults(List<ScanResult> results) {
                    for (ScanResult sr : results) {
                        Log.i("ScanResult - Results", sr.toString());
                    }
                }

                @Override
                public void onScanFailed(int errorCode) {
                    Log.e("Scan Failed", "Error Code: " + errorCode);
                }
            };
        }

        Log.e(TAG, "Initialize BluetoothAdapter success.");
        return mBluetoothAdapter;
    }

    public static void bleScanPeripherals(Context context, String uuid)
    {
        Log.e(TAG, "Trying to bleScanPeripherals, uuid: " + uuid);
        if (Build.VERSION.SDK_INT < 21) {
            mBluetoothAdapter.startLeScan(mLeScanCallback);
        } else {
            ScanSettings settings = new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build();
            List<ScanFilter> filters = new ArrayList<ScanFilter>();
            //  An app must hold ACCESS_COARSE_LOCATION or ACCESS_FINE_LOCATION permission in order to get results.
            mLEScanner.startScan(filters, settings, mScanCallback);
        }

        Log.e(TAG, "Trying to bleScanPeripherals, Device API-" + Build.VERSION.SDK_INT);
    }

    public static void bleStopScanPeripherals(Context context)
    {
        Log.e(TAG, "Trying to bleStopScanPeripherals");
        if (Build.VERSION.SDK_INT < 21) {
            mBluetoothAdapter.stopLeScan(mLeScanCallback);
        } else {
            mLEScanner.stopScan(mScanCallback);
        }
    }

    //
    // use as peripheral section
    //
    public static native void nativePConnectionStateChange(String address, boolean connected);
    public static native void nativePCharacteristicRead(String chara, byte[] buffer, boolean notify);

    /**
     * 以字符串表示形式返回字节数组的内容
     *
     * @param bytes 字节数组
     * @return 字符串形式的 <tt>bytes</tt>
     * [01, fe, 08, 35, f1, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00]
     */
    public static String toHexString(byte[] bytes) {
        if (bytes == null)
            return "null";
        int iMax = bytes.length - 1;
        if (iMax == -1)
            return "[]";
        StringBuilder b = new StringBuilder();
        b.append('[');
        for (int i = 0; ; i++) {
            b.append(String.format("%02x", bytes[i] & 0xFF));
            if (i == iMax)
                return b.append(']').toString();
            b.append(", ");
        }
    }

    public static final int REQUEST_ENABLE_BT = 1;
    public static final int REQUEST_CODE = 2;
    public static final UUID UUID_SERVER = UUID.fromString(UuidUtils.uuid16To128("fd00"));
    public static final UUID UUID_CHARWRITE = UUID.fromString(UuidUtils.uuid16To128("fd01"));
    public static final UUID UUID_CHARREAD = UUID.fromString(UuidUtils.uuid16To128("fd02"));
    public static final UUID UUID_NOTIFY = UUID.fromString(UuidUtils.uuid16To128("fd03"));
    public static final UUID UUID_DESCRIPTOR = UUID.fromString(UuidUtils.uuid16To128("2902"));

    private boolean blePeripheralInitialized = false;
    private boolean advertisering = false;

    private BluetoothDevice mBleDevice;

    private void bleInitPeripheral() {
        blePeripheralInitialized = true;
        if (mBluetoothManager == null) {
            bleInitManager();
        }
        if (mBluetoothAdapter == null) {
            return;
        }

        BluetoothLeAdvertiser mAdvertiser = mBluetoothAdapter.getBluetoothLeAdvertiser();
        if (mAdvertiser == null) {
            Log.d(TAG,"设备不支持广播蓝牙");
            finish();
        }
        // mBluetoothAdapter.setName("RDP Discovery");
    }

    private void startAdvertiser(String name, int manufacturerId, byte[] manufacturerData) {
        if (!blePeripheralInitialized) {
            bleInitPeripheral();
        }
        // 测试下来, setName参数传空字符串时(不是null, null会导致app非法退出)，虽不报错但其实无影响, 这里为简单还是不判断吧
        // 但这样一来, 希望上面代码不传null/""下来
        String name2 = name != null? name: "";
        mBluetoothAdapter.setName(name2);

        // ADVERTISE_MODE_LOW_LATENCY ==> 100ms
        // ADVERTISE_MODE_BALANCED ==> 250ms
        // ADVERTISE_MODE_LOW_POWER ==> 1s
        // 网上写的是这间隔，但我在rk3399-aio测下来都大概是100ms, 不知道是哪里没设对. 但不管怎样设一个最大间隔ADVERTISE_MODE_LOW_POWER
        AdvertiseSettings settings = new AdvertiseSettings.Builder()
                .setAdvertiseMode(AdvertiseSettings.ADVERTISE_MODE_LOW_POWER)//设置广播间隔100ms
                .setConnectable(true)
                .setTimeout(0)
                .setTxPowerLevel(AdvertiseSettings.ADVERTISE_TX_POWER_HIGH)
                .build();
/*
        AdvertiseData advertiseData = new AdvertiseData.Builder()
                .setIncludeDeviceName(true)
                .setIncludeTxPowerLevel(true)
                .addManufacturerData(65520, new byte[]{'J', 'Z', '6', '7', '8', '9'})
                .build();
*/
        AdvertiseData.Builder builder = new AdvertiseData.Builder()
                .setIncludeDeviceName(true)
                .setIncludeTxPowerLevel(true);
        if (manufacturerData != null) {
            builder.addManufacturerData(manufacturerId, manufacturerData);
        }
        AdvertiseData advertiseData = builder.build();

        AdvertiseData scanResponseData = new AdvertiseData.Builder()
                .addServiceUuid(new ParcelUuid(UUID_SERVER))
                .setIncludeTxPowerLevel(true)
                .build();

        BluetoothLeAdvertiser bluetoothLeAdvertiser = mBluetoothAdapter.getBluetoothLeAdvertiser();
        if (bluetoothLeAdvertiser == null){
            Log.d(TAG,"设备不支持广播蓝牙");
            return;
        }
        bluetoothLeAdvertiser.startAdvertising(settings, advertiseData, scanResponseData, callback);
        {
            int ii = 0;
            advertisering = true;
        }
    }

    private void stopAdvertiser(){
        bluetoothGattServer.clearServices();
        BluetoothLeAdvertiser bluetoothLeAdvertiser = mBluetoothAdapter.getBluetoothLeAdvertiser();
        bluetoothLeAdvertiser.stopAdvertising(callback);
        advertisering = false;
        Log.d(TAG,"开启广播");
        bluetoothGattServer.clearServices();
        bluetoothGattServer.close();
    }

    AdvertiseCallback callback = new AdvertiseCallback() {
        @Override
        public void onStartSuccess(AdvertiseSettings settingsInEffect) {
            Log.d(TAG, "BLE advertisement added successfully");
            initServices(getBaseContext());
            advertisering = true;
            // btn_start_ad.setText("停止广播");
        }

        @Override
        public void onStartFailure(int errorCode) {
            Log.d(TAG, "Failed to add BLE advertisement, reason: " + errorCode);
            advertisering = false;
        }
    };

    private BluetoothGattServer bluetoothGattServer;
    private BluetoothGattCharacteristic characteristicNotify;
    private void initServices(Context context) {
        bluetoothGattServer = mBluetoothManager.openGattServer(context, bluetoothGattServerCallback);
        BluetoothGattService service = new BluetoothGattService(UUID_SERVER, BluetoothGattService.SERVICE_TYPE_PRIMARY);

        //add a read characteristic.
        BluetoothGattCharacteristic characteristicRead = new BluetoothGattCharacteristic(UUID_CHARREAD, BluetoothGattCharacteristic.PROPERTY_READ, BluetoothGattCharacteristic.PERMISSION_READ);
        service.addCharacteristic(characteristicRead);

        characteristicNotify = new BluetoothGattCharacteristic(UUID_NOTIFY, BluetoothGattCharacteristic.PROPERTY_NOTIFY, BluetoothGattCharacteristic.PROPERTY_WRITE);
        //add a descriptor
        BluetoothGattDescriptor descriptor = new BluetoothGattDescriptor(UUID_DESCRIPTOR, BluetoothGattCharacteristic.PERMISSION_WRITE);
        characteristicNotify.addDescriptor(descriptor);
        service.addCharacteristic(characteristicNotify);

        //add a write characteristic.
        BluetoothGattCharacteristic characteristicWrite = new BluetoothGattCharacteristic(UUID_CHARWRITE,
                BluetoothGattCharacteristic.PROPERTY_WRITE |
                        BluetoothGattCharacteristic.PROPERTY_READ |
                        BluetoothGattCharacteristic.PROPERTY_NOTIFY,
                BluetoothGattCharacteristic.PERMISSION_WRITE);
        service.addCharacteristic(characteristicWrite);

        bluetoothGattServer.addService(service);
        Log.d(TAG, "2. initServices ok");
        // toast("2. initServices ok");
    }

    /**
     * 服务事件的回调
     */
    private BluetoothGattServerCallback bluetoothGattServerCallback = new BluetoothGattServerCallback() {
        /**
         * 1.连接状态发生变化时
         * @param device
         * @param status
         * @param newState
         */
        @Override
        public void onConnectionStateChange(final BluetoothDevice device, int status, final int newState) {
            Log.d(TAG, String.format("1.onConnectionStateChange：device name = %s, address = %s", device.getName(), device.getAddress()));
            Log.d(TAG, String.format("1.onConnectionStateChange：status = %s, newState =%s ", status, newState));
            super.onConnectionStateChange(device, status, newState);

            if (newState == BluetoothProfile.STATE_CONNECTED){
                mBleDevice = device;
                Log.d(TAG, String.format("已连接: %s", device.getAddress()));
                nativePConnectionStateChange(device.getAddress(), true);
            }else {
                mBleDevice = null;
                Log.d(TAG, "未连接:");
                nativePConnectionStateChange(null, false);
            }

        }

        @Override
        public void onServiceAdded(int status, BluetoothGattService service) {
            super.onServiceAdded(status, service);
            Log.d(TAG, String.format("onServiceAdded：status = %s", status));
        }

        @Override
        public void onCharacteristicReadRequest(BluetoothDevice device, int requestId, int offset, BluetoothGattCharacteristic characteristic) {
            Log.d(TAG, String.format("onCharacteristicReadRequest：device name = %s, address = %s", device.getName(), device.getAddress()));
            Log.d(TAG, String.format("onCharacteristicReadRequest：requestId = %s, offset = %s", requestId, offset));

            bluetoothGattServer.sendResponse(device, requestId, BluetoothGatt.GATT_SUCCESS, offset, characteristic.getValue());

//            super.onCharacteristicReadRequest(device, requestId, offset, characteristic);
        }

        /**
         * 3. onCharacteristicWriteRequest,接收具体的字节
         * @param device
         * @param requestId
         * @param characteristic
         * @param preparedWrite
         * @param responseNeeded
         * @param offset
         * @param requestBytes
         */
        @Override
        public void onCharacteristicWriteRequest(BluetoothDevice device, int requestId, BluetoothGattCharacteristic characteristic, boolean preparedWrite, boolean responseNeeded, int offset, byte[] requestBytes) {
            Log.d(TAG, String.format("3.onCharacteristicWriteRequest：device name = %s, address = %s, uuid = %s", device.getName(), device.getAddress(), characteristic.getUuid().toString()));
            Log.d(TAG, String.format("3.onCharacteristicWriteRequest：requestId = %s, preparedWrite=%s, responseNeeded=%s, offset=%s, value=%s", requestId, preparedWrite, responseNeeded, offset, toHexString(requestBytes)));
            bluetoothGattServer.sendResponse(device, requestId, BluetoothGatt.GATT_SUCCESS, offset, requestBytes);

            nativePCharacteristicRead(characteristic.getUuid().toString(), requestBytes, responseNeeded);
            final String msg = toHexString(requestBytes);
            Log.d(TAG, "4.收到:" + msg);

            //4.处理响应内容
            // onResponseToClient(requestBytes, device);
        }

        /**
         * 2.描述被写入时，在这里执行 bluetoothGattServer.sendResponse(device, requestId, BluetoothGatt.GATT_SUCCESS...  收，触发 onCharacteristicWriteRequest
         * @param device
         * @param requestId
         * @param descriptor
         * @param preparedWrite
         * @param responseNeeded
         * @param offset
         * @param value
         */
        @Override
        public void onDescriptorWriteRequest(BluetoothDevice device, int requestId, BluetoothGattDescriptor descriptor, boolean preparedWrite, boolean responseNeeded, int offset, byte[] value) {
            Log.d(TAG, String.format("2.onDescriptorWriteRequest：device name = %s, address = %s", device.getName(), device.getAddress()));
            Log.d(TAG, String.format("2.onDescriptorWriteRequest：requestId = %s, preparedWrite = %s, responseNeeded = %s, offset = %s, value = %s,", requestId, preparedWrite, responseNeeded, offset, toHexString(value)));

            // now tell the connected device that this was all successfull
            bluetoothGattServer.sendResponse(device, requestId, BluetoothGatt.GATT_SUCCESS, offset, value);
        }

        /**
         * 5.特征被读取。当回复响应成功后，客户端会读取然后触发本方法
         * @param device
         * @param requestId
         * @param offset
         * @param descriptor
         */
        @Override
        public void onDescriptorReadRequest(BluetoothDevice device, int requestId, int offset, BluetoothGattDescriptor descriptor) {
            Log.d(TAG, String.format("onDescriptorReadRequest：device name = %s, address = %s", device.getName(), device.getAddress()));
            Log.d(TAG, String.format("onDescriptorReadRequest：requestId = %s", requestId));
//            super.onDescriptorReadRequest(device, requestId, offset, descriptor);
            bluetoothGattServer.sendResponse(device, requestId, BluetoothGatt.GATT_SUCCESS, offset, null);
        }

        @Override
        public void onNotificationSent(BluetoothDevice device, int status) {
            super.onNotificationSent(device, status);
            Log.d(TAG, String.format("5.onNotificationSent：device name = %s, address = %s", device.getName(), device.getAddress()));
            Log.d(TAG, String.format("5.onNotificationSent：status = %s", status));
        }

        @Override
        public void onMtuChanged(BluetoothDevice device, int mtu) {
            super.onMtuChanged(device, mtu);
            Log.d(TAG, String.format("onMtuChanged：mtu = %s", mtu));
        }

        @Override
        public void onExecuteWrite(BluetoothDevice device, int requestId, boolean execute) {
            super.onExecuteWrite(device, requestId, execute);
            Log.d(TAG, String.format("onExecuteWrite：requestId = %s", requestId));
        }
    };

    /**
     * 4.处理响应内容
     *
     * @param data
     * @param device
     * @param //requestId
     */
    private void onResponseToClient(byte[] data, BluetoothDevice device) {
        Log.e(TAG, String.format("4.onResponseToClient：device name = %s, address = %s", device.getName(), device.getAddress()));
//        Log.e(TAG, String.format("4.onResponseToClient：requestId = %s", requestId));
        characteristicNotify.setValue(data);
        //通知数据改变
        bluetoothGattServer.notifyCharacteristicChanged(device, characteristicNotify, false);
        Log.d(TAG, "4.响应:" + toHexString(data));
    }

    public static void pbleStartAdvertising(Context context, String name, int manufacturerId, byte[] manufacturerData) {
        SDLActivity.mSingleton.startAdvertiser(name, manufacturerId, manufacturerData);
    }

    public static void pbleStopAdvertising(Context context) {
        SDLActivity.mSingleton.stopAdvertiser();
    }

    public static boolean pbleIsAdvertising(Context context) {
        return SDLActivity.mSingleton.advertisering;
    }

    public static void pbleWriteCharacteristic(Context context, String chara, byte[] data) {
        Log.d(TAG, "pbleWriteCharacteristic: " + toHexString(data) + " mBleDevice: " + (SDLActivity.mSingleton.mBleDevice != null? "isn't null": "is null"));
        if (SDLActivity.mSingleton.mBleDevice != null) {
            SDLActivity.mSingleton.onResponseToClient(data, SDLActivity.mSingleton.mBleDevice);
        }
    }

    // Helper method for building a string of thread information.
    public static String getThreadInfo() {
        return "@[name=" + Thread.currentThread().getName() + ", id=" + Thread.currentThread().getId()
                + "]";
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        if (requestCode != MY_PERMISSIONS_REQUEST) {
            return;
        }
        // If request is cancelled, the result arrays are empty.
        int length = grantResults.length;
        boolean re_request = false;
        String ungrant_permissions_desc = "";
        for (int i = 0; i < length; i++) {
            if (grantResults[i] != PackageManager.PERMISSION_GRANTED) {
                if (!ungrant_permissions_desc.isEmpty()) {
                    ungrant_permissions_desc += ", ";
                }
                ungrant_permissions_desc += permissions[i];
                re_request = true;
            }
        }
        if (re_request) {
            // User has denied some persmission, require user allow them manually.
            final AlertDialog.Builder dlgAlert  = new AlertDialog.Builder(this);
            dlgAlert.setMessage("Permissions(" + ungrant_permissions_desc + ") are denied. You need to manually allow in Settings."
                    + System.getProperty("line.separator")
                    + System.getProperty("line.separator")
                    + "Click Settings, Jump to Settings.\nClick Exit, Exit app.");
            dlgAlert.setTitle("Grant Permissoin");
            dlgAlert.setNegativeButton("Settings",
                    new DialogInterface.OnClickListener() {
                        @Override
                        public void onClick(DialogInterface dialog,int id) {
                            // if this button is clicked, jump to Application Detail, then close current activity
                            Uri packageURI = Uri.parse("package:" + getPackageName());
                            Intent intent =  new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS,packageURI);
                            startActivity(intent);

                            SDLActivity.mSingleton.finish();
                        }
                    });
            dlgAlert.setPositiveButton("Exit",
                    new DialogInterface.OnClickListener() {
                        @Override
                        public void onClick(DialogInterface dialog,int id) {
                            // if this button is clicked, close current activity
                            SDLActivity.mSingleton.finish();
                        }
                    });
            dlgAlert.setCancelable(false);
            dlgAlert.create().show();
        }
    }

    public static void checkPermission(Context mContext, ArrayList<String> permissionList)
    {
        if (permissionList == null || permissionList.size() == 0) {
            return;
        }

        ArrayList<String> list = new ArrayList<>(permissionList);
        Iterator<String> it = list.iterator();
        while (it.hasNext()) {
            String permission = it.next();
            int hasPermission = ContextCompat.checkSelfPermission(mContext, permission);
            if (hasPermission == PackageManager.PERMISSION_GRANTED) {
                it.remove();
            }
        }

        if (list.size() == 0) {
            return;
        }
        String[] permissions = list.toArray(new String[0]);
        ActivityCompat.requestPermissions((Activity) mContext, permissions, MY_PERMISSIONS_REQUEST);
    }

    public static int getStatusBarHeight(Context context) {
        Resources resources = context.getResources();
        int resourceId = resources.getIdentifier("status_bar_height", "dimen","android");
        int height = resources.getDimensionPixelSize(resourceId);
        return height;
    }

    public static void showStatusBar(Context context, boolean visible) {
        setWindowStyle(visible == true? false: true);
    }

    protected void immerse_statusbar(Context activity)
    {
        Window window = ((Activity)activity).getWindow();
        // 设置透明状态栏,这样才能让 ContentView 向上
        window.addFlags(WindowManager.LayoutParams.FLAG_TRANSLUCENT_STATUS);

        //需要设置这个 flag 才能调用 setStatusBarColor 来设置状态栏颜色
        window.addFlags(WindowManager.LayoutParams.FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS);
        //设置状态栏颜色
        window.setStatusBarColor(Color.TRANSPARENT);

        ViewGroup mContentView = (ViewGroup) ((Activity)activity).findViewById(Window.ID_ANDROID_CONTENT);
        View mChildView = mContentView.getChildAt(0);
        if (mChildView != null) {
            //注意不是设置 ContentView 的 FitsSystemWindows, 而是设置 ContentView 的第一个子 View . 使其不为系统 View 预留空间.
            ViewCompat.setFitsSystemWindows(mChildView, false);
        }
    }

    // Setup
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        Log.i(TAG, "[com.kos.launcher]onCreate()");
        Log.i(TAG, "Device: " + android.os.Build.DEVICE);
        Log.i(TAG, "Model: " + android.os.Build.MODEL);
        super.onCreate(savedInstanceState);

        // Load shared libraries
        String errorMsgBrokenLib = "";
        try {
            loadLibraries();
        } catch(UnsatisfiedLinkError e) {
            System.err.println(e.getMessage());
            mBrokenLibraries = true;
            errorMsgBrokenLib = e.getMessage();
        } catch(Exception e) {
            System.err.println(e.getMessage());
            mBrokenLibraries = true;
            errorMsgBrokenLib = e.getMessage();
        }

        if (mBrokenLibraries)
        {
            mSingleton = this;
            AlertDialog.Builder dlgAlert  = new AlertDialog.Builder(this);
            dlgAlert.setMessage("An error occurred while trying to start the application. Please try again and/or reinstall."
                  + System.getProperty("line.separator")
                  + System.getProperty("line.separator")
                  + "Error: " + errorMsgBrokenLib);
            dlgAlert.setTitle("SDL Error");
            dlgAlert.setPositiveButton("Exit",
                new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface dialog,int id) {
                        // if this button is clicked, close current activity
                        SDLActivity.mSingleton.finish();
                    }
                });
           dlgAlert.setCancelable(false);
           dlgAlert.create().show();

           return;
        }

        // Set up JNI
        SDL.setupJNI();

        // Initialize state
        SDL.initialize();

        // So we can call stuff from static callbacks
        mSingleton = this;
        SDL.setContext(this);

        mPermissionList.add(Manifest.permission.CAMERA);
        mPermissionList.add(Manifest.permission.RECORD_AUDIO);
        mPermissionList.add(Manifest.permission.MODIFY_AUDIO_SETTINGS);
        mPermissionList.add(Manifest.permission.INTERNET);
        mPermissionList.add(Manifest.permission.WRITE_EXTERNAL_STORAGE);
        checkPermission(this, mPermissionList);

        if (Build.VERSION.SDK_INT >= 11) {
            mClipboardHandler = new SDLClipboardHandler_API11();
        } else {
            /* Before API 11, no clipboard notification (eg no SDL_CLIPBOARDUPDATE) */
            mClipboardHandler = new SDLClipboardHandler_Old();
        }

        // Set up the surface
        mSurface = new SDLSurface(getApplication());

        mLayout = new RelativeLayout(this);
        mLayout.addView(mSurface);

        setContentView(mLayout);
        getWindow().getDecorView().setOnSystemUiVisibilityChangeListener(this);

        setWindowStyle(true);

        // Get filename from "Open with" of another application
        Intent intent = getIntent();
        if (intent != null && intent.getData() != null) {
            String filename = intent.getData().getPath();
            if (filename != null) {
                Log.v(TAG, "Got filename: " + filename);
                SDLActivity.onNativeDropFile(filename);
            }
        }

        boolean manualLockScreen = false;
        mPowerManager = (PowerManager) getSystemService(POWER_SERVICE);
        // active device policy manager must execute in UI thread.
        // and must execute before SDLMain thread startup.
        // I no way to tell android base SDLMain, so judge subjectively.
        if (manualLockScreen) {
            activeDevicePolicyManager();
        }
    }

    // Events
    @Override
    protected void onPause() {
        Log.v(TAG, "onPause()");
        super.onPause();
        mNextNativeState = NativeState.PAUSED;
        mIsResumedCalled = false;

        if (SDLActivity.mBrokenLibraries) {
           return;
        }

        SDLActivity.handleNativeState();
    }

    @Override
    protected void onResume() {
        Log.v(TAG, "onResume()");
        super.onResume();
        mNextNativeState = NativeState.RESUMED;
        mIsResumedCalled = true;

        if (SDLActivity.mBrokenLibraries) {
           return;
        }

        SDLActivity.handleNativeState();
    }


    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        Log.v(TAG, "onWindowFocusChanged(): " + hasFocus);

        if (SDLActivity.mBrokenLibraries) {
           return;
        }

        SDLActivity.mHasFocus = hasFocus;
        if (hasFocus) {
           mNextNativeState = NativeState.RESUMED;
        } else {
           mNextNativeState = NativeState.PAUSED;
        }

        SDLActivity.handleNativeState();
    }

    @Override
    public void onLowMemory() {
        Log.v(TAG, "onLowMemory()");
        super.onLowMemory();

        if (SDLActivity.mBrokenLibraries) {
           return;
        }

        SDLActivity.nativeLowMemory();
    }

    @Override
    protected void onDestroy() {
        Log.v(TAG, "onDestroy()");

        if (SDLActivity.mBrokenLibraries) {
           super.onDestroy();
           // Reset everything in case the user re opens the app
           SDLActivity.initialize();
           return;
        }

        mNextNativeState = NativeState.PAUSED;
        SDLActivity.handleNativeState();

        // Send a quit message to the application
        SDLActivity.mExitCalledFromJava = true;
        if (!mOrientationResult) {
            SDLActivity.nativeQuit();

            // Now wait for the SDL thread to quit
            if (SDLActivity.mSDLThread != null) {
/*              try {
                    SDLActivity.mSDLThread.join();
                } catch (Exception e) {
                    Log.v(TAG, "Problem stopping thread: " + e);
                }
*/
                SDLActivity.mSDLThread = null;

                //Log.v(TAG, "Finished waiting for SDL thread");
            }
        }

        super.onDestroy();

        // Reset everything in case the user re opens the app
        SDLActivity.initialize();
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {

        if (SDLActivity.mBrokenLibraries) {
           return false;
        }

        int keyCode = event.getKeyCode();
        // Ignore certain special keys so they're handled by Android
        if (keyCode == KeyEvent.KEYCODE_VOLUME_DOWN ||
            keyCode == KeyEvent.KEYCODE_VOLUME_UP ||
            keyCode == KeyEvent.KEYCODE_CAMERA ||
            keyCode == KeyEvent.KEYCODE_ZOOM_IN || /* API 11 */
            keyCode == KeyEvent.KEYCODE_ZOOM_OUT /* API 11 */
            ) {
            return false;
        }
        return super.dispatchKeyEvent(event);
    }

    /* Transition to next state */
    public static void handleNativeState() {

        if (mNextNativeState == mCurrentNativeState) {
            // Already in same state, discard.
            return;
        }

        // Try a transition to init state
        if (mNextNativeState == NativeState.INIT) {

            mCurrentNativeState = mNextNativeState;
            return;
        }

        // Try a transition to paused state
        if (mNextNativeState == NativeState.PAUSED) {
            nativePause();
            if (mSurface != null)
                mSurface.handlePause();
            mCurrentNativeState = mNextNativeState;
            return;
        }

        // Try a transition to resumed state
        if (mNextNativeState == NativeState.RESUMED) {
            if (mIsSurfaceReady && mHasFocus && mIsResumedCalled) {
                if (mSDLThread == null) {
                    // This is the entry point to the C app.
                    // Start up the C app thread and enable sensor input for the first time
                    // FIXME: Why aren't we enabling sensor input at start?

                    mSDLThread = new Thread(new SDLMain(), "SDLThread");
                    mSurface.enableSensor(Sensor.TYPE_ACCELEROMETER, true);
                    mSDLThread.start();
                }

                nativeResume();
                mSurface.handleResume();
                mCurrentNativeState = mNextNativeState;
            }
        }
    }

    /* The native thread has finished */
    public static void handleNativeExit() {
        SDLActivity.mSDLThread = null;
        mSingleton.finish();
    }


    // Messages from the SDLMain thread
    static final int COMMAND_CHANGE_TITLE = 1;
    static final int COMMAND_CHANGE_WINDOW_STYLE = 2;
    static final int COMMAND_TEXTEDIT_HIDE = 3;
    static final int COMMAND_SET_KEEP_SCREEN_ON = 5;

    protected static final int COMMAND_USER = 0x8000;

    protected static boolean mFullscreenModeActive;

    /**
     * This method is called by SDL if SDL did not handle a message itself.
     * This happens if a received message contains an unsupported command.
     * Method can be overwritten to handle Messages in a different class.
     * @param command the command of the message.
     * @param param the parameter of the message. May be null.
     * @return if the message was handled in overridden method.
     */
    protected boolean onUnhandledMessage(int command, Object param) {
        return false;
    }

    /**
     * A Handler class for Messages from native SDL applications.
     * It uses current Activities as target (e.g. for the title).
     * static to prevent implicit references to enclosing object.
     */
    protected static class SDLCommandHandler extends Handler {
        @Override
        public void handleMessage(Message msg) {
            Context context = SDL.getContext();
            if (context == null) {
                Log.e(TAG, "error handling message, getContext() returned null");
                return;
            }
            switch (msg.arg1) {
            case COMMAND_CHANGE_TITLE:
                if (context instanceof Activity) {
                    ((Activity) context).setTitle((String)msg.obj);
                } else {
                    Log.e(TAG, "error handling message, getContext() returned no Activity");
                }
                break;
            case COMMAND_CHANGE_WINDOW_STYLE:
                if (Build.VERSION.SDK_INT < 19) {
                    // This version of Android doesn't support the immersive fullscreen mode
                    break;
                }
// This needs more testing, per bug 4096 - Enabling fullscreen on Android causes the app to toggle fullscreen mode continuously in a loop

                if (context instanceof Activity) {
                    Window window = ((Activity) context).getWindow();
                    if (window != null) {
                        if ((msg.obj instanceof Integer) && (((Integer) msg.obj).intValue() != 0)) {
                            int flags = View.SYSTEM_UI_FLAG_FULLSCREEN |
                                    View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
                                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                                    View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
                                    View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
                                    View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.INVISIBLE;
                            window.getDecorView().setSystemUiVisibility(flags);
                            window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
                            window.clearFlags(WindowManager.LayoutParams.FLAG_FORCE_NOT_FULLSCREEN);
                            SDLActivity.mFullscreenModeActive = true;
                        } else {
                            // when screen size if 1920x1080, if use SDL's NOT_FULLSCREEN, Window size: 1920x1007.
                            // if use immerse_statusbar, Window size: 1920x1032, I use it.
                            mSingleton.immerse_statusbar(context);
/*
                            int flags = View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_VISIBLE;
                            window.getDecorView().setSystemUiVisibility(flags);
                            window.addFlags(WindowManager.LayoutParams.FLAG_FORCE_NOT_FULLSCREEN);
                            window.clearFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
*/
                            SDLActivity.mFullscreenModeActive = false;
                        }
                    }
                } else {
                    Log.e(TAG, "error handling message, getContext() returned no Activity");
                }
//
                break;
            case COMMAND_TEXTEDIT_HIDE:
                if (mTextEdit != null) {
                    // Note: On some devices setting view to GONE creates a flicker in landscape.
                    // Setting the View's sizes to 0 is similar to GONE but without the flicker.
                    // The sizes will be set to useful values when the keyboard is shown again.
                    mTextEdit.setLayoutParams(new RelativeLayout.LayoutParams(0, 0));

                    InputMethodManager imm = (InputMethodManager) context.getSystemService(Context.INPUT_METHOD_SERVICE);
                    imm.hideSoftInputFromWindow(mTextEdit.getWindowToken(), 0);

                    mScreenKeyboardShown = false;
                }
                break;
            case COMMAND_SET_KEEP_SCREEN_ON:
            {
                if (context instanceof Activity) {
                    Window window = ((Activity) context).getWindow();
                    if (window != null) {
                        if ((msg.obj instanceof Integer) && (((Integer) msg.obj).intValue() != 0)) {
                            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                        } else {
                            window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                        }
                    }
                }
                break;
            }
            default:
                if ((context instanceof SDLActivity) && !((SDLActivity) context).onUnhandledMessage(msg.arg1, msg.obj)) {
                    Log.e(TAG, "error handling message, command is " + msg.arg1);
                }
            }
        }
    }

    // Handler for the messages
    Handler commandHandler = new SDLCommandHandler();

    // Send a message from the SDLMain thread
    boolean sendCommand(int command, Object data) {
        Message msg = commandHandler.obtainMessage();
        msg.arg1 = command;
        msg.obj = data;
        return commandHandler.sendMessage(msg);
    }

    // C functions we call
    public static native int nativeSetupJNI();
    public static native int nativeRunMain(String library, String function, Object arguments, int SDK_INT);
    public static native void nativeLowMemory();
    public static native void nativeQuit();
    public static native void nativePause();
    public static native void nativeResume();
    public static native void onNativeDropFile(String filename);
    public static native void onNativeResize(int x, int y, int format, float rate);
    public static native void onNativeKeyDown(int keycode);
    public static native void onNativeKeyUp(int keycode);
    public static native void onNativeKeyboardFocusLost();
    public static native void onNativeMouse(int button, int action, float x, float y);
    public static native void onNativeTouch(int touchDevId, int pointerFingerId,
                                            int action, float x,
                                            float y, float p);
    public static native void onNativeAccel(float x, float y, float z);
    public static native void onNativeClipboardChanged();
    public static native void onNativeSurfaceChanged();
    public static native void onNativeSurfaceDestroyed();
    public static native String nativeGetHint(String name);
    public static native void nativeSetenv(String name, String value);

    /**
     * This method is called by SDL using JNI.
     */
    public static boolean setActivityTitle(String title) {
        // Called from SDLMain() thread and can't directly affect the view
        return mSingleton.sendCommand(COMMAND_CHANGE_TITLE, title);
    }

    /**
     * This method is called by SDL using JNI.
     */
    public static void setWindowStyle(boolean fullscreen) {
        // Called from SDLMain() thread and can't directly affect the view
        mSingleton.sendCommand(COMMAND_CHANGE_WINDOW_STYLE, fullscreen ? 1 : 0);
    }

    /**
     * This method is called by SDL using JNI.
     * This is a static method for JNI convenience, it calls a non-static method
     * so that is can be overridden  
     */
    public static void setOrientation(int w, int h, boolean resizable, String hint)
    {
        if (mSingleton != null) {
            mSingleton.setOrientationBis(w, h, resizable, hint);
        }
    }
   
    /**
     * This can be overridden
     */
    public void setOrientationBis(int w, int h, boolean resizable, String hint) 
    {
        int orientation = -1;

        if (hint.contains("LandscapeRight") && hint.contains("LandscapeLeft")) {
            orientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE;
        } else if (hint.contains("LandscapeRight")) {
            orientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE;
        } else if (hint.contains("LandscapeLeft")) {
            orientation = ActivityInfo.SCREEN_ORIENTATION_REVERSE_LANDSCAPE;
        } else if (hint.contains("Portrait") && hint.contains("PortraitUpsideDown")) {
            orientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT;
        } else if (hint.contains("Portrait")) {
            orientation = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT;
        } else if (hint.contains("PortraitUpsideDown")) {
            orientation = ActivityInfo.SCREEN_ORIENTATION_REVERSE_PORTRAIT;
        }

        /* no valid hint */
        if (orientation == -1) {
            if (resizable) {
                /* no fixed orientation */
            } else {
                if (w > h) {
                    orientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE;
                } else {
                    orientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT;
                }
            }
        }

        Log.v("SDL", "setOrientation() orientation=" + orientation + " width=" + w +" height="+ h +" resizable=" + resizable + " hint=" + hint);
        if (orientation != -1) {
            mSingleton.setRequestedOrientation(orientation);
            mOrientationResult = true;
        }
    }


    /**
     * This method is called by SDL using JNI.
     */
    public static boolean isScreenKeyboardShown() 
    {
        if (mTextEdit == null) {
            return false;
        }

        if (!mScreenKeyboardShown) {
            return false;
        }

        InputMethodManager imm = (InputMethodManager) SDL.getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
        return imm.isAcceptingText();

    }

    public static boolean canScreenKeyboardShown()
    {
        // copy from: public boolean onEvaluateInputViewShown()
        // <aosp>/frameworks/base/core/java/android/inputmethodservice/InputMethodService.java
        Configuration config = SDLActivity.mSingleton.getResources().getConfiguration();
        return config.keyboard == Configuration.KEYBOARD_NOKEYS  || config.hardKeyboardHidden == Configuration.HARDKEYBOARDHIDDEN_YES;
    }

    /**
     * This method is called by SDL using JNI.
     */
    public static boolean sendMessage(int command, int param) {
        if (mSingleton == null) {
            return false;
        }
        return mSingleton.sendCommand(command, Integer.valueOf(param));
    }

    /**
     * This method is called by SDL using JNI.
     */
    public static Context getContext() {
        return SDL.getContext();
    }

    /**
     * This method is called by SDL using JNI.
     */
    public static boolean isAndroidTV() {
        UiModeManager uiModeManager = (UiModeManager) getContext().getSystemService(UI_MODE_SERVICE);
        return (uiModeManager.getCurrentModeType() == Configuration.UI_MODE_TYPE_TELEVISION);
    }

    /**
     * This method is called by SDL using JNI.
     */
    public static DisplayMetrics getDisplayDPI() {
        return getContext().getResources().getDisplayMetrics();
    }

    /**
     * This method is called by SDL using JNI.
     */
    public static boolean getManifestEnvironmentVariables() {
        try {
            ApplicationInfo applicationInfo = getContext().getPackageManager().getApplicationInfo(getContext().getPackageName(), PackageManager.GET_META_DATA);
            Bundle bundle = applicationInfo.metaData;
            if (bundle == null) {
                return false;
            }
            String prefix = "SDL_ENV.";
            final int trimLength = prefix.length();
            for (String key : bundle.keySet()) {
                if (key.startsWith(prefix)) {
                    String name = key.substring(trimLength);
                    String value = bundle.get(key).toString();
                    nativeSetenv(name, value);
                }
            }
            /* environment variables set! */
            return true; 
        } catch (Exception e) {
           Log.v("SDL", "exception " + e.toString());
        }
        return false;
    }

    static class ShowTextInputTask implements Runnable {
        /*
         * This is used to regulate the pan&scan method to have some offset from
         * the bottom edge of the input region and the top edge of an input
         * method (soft keyboard)
         */
        static final int HEIGHT_PADDING = 15;

        public int x, y, w, h;

        public ShowTextInputTask(int x, int y, int w, int h) {
            this.x = x;
            this.y = y;
            this.w = w;
            this.h = h;
        }

        @Override
        public void run() {
            RelativeLayout.LayoutParams params = new RelativeLayout.LayoutParams(w, h + HEIGHT_PADDING);
            params.leftMargin = x;
            params.topMargin = y;

            if (mTextEdit == null) {
                mTextEdit = new DummyEdit(SDL.getContext());

                mLayout.addView(mTextEdit, params);
            } else {
                mTextEdit.setLayoutParams(params);
            }

            mTextEdit.setVisibility(View.VISIBLE);
            mTextEdit.requestFocus();

            InputMethodManager imm = (InputMethodManager) SDL.getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
            imm.showSoftInput(mTextEdit, 0);

            mScreenKeyboardShown = true;
        }
    }

    /**
     * This method is called by SDL using JNI.
     */
    public static boolean showTextInput(int x, int y, int w, int h) {
        // Transfer the task to the main thread as a Runnable
        return mSingleton.commandHandler.post(new ShowTextInputTask(x, y, w, h));
    }

    public static boolean isTextInputEvent(KeyEvent event) {
      
        // Key pressed with Ctrl should be sent as SDL_KEYDOWN/SDL_KEYUP and not SDL_TEXTINPUT
        if (Build.VERSION.SDK_INT >= 11) {
            if (event.isCtrlPressed()) {
                return false;
            }  
        }

        return event.isPrintingKey() || event.getKeyCode() == KeyEvent.KEYCODE_SPACE;
    }

    /**
     * This method is called by SDL using JNI.
     */
    public static Surface getNativeSurface() {
        if (SDLActivity.mSurface == null) {
            return null;
        }
        return SDLActivity.mSurface.getNativeSurface();
    }

    // Audio

    /**
     * This method is called by SDL using JNI.
     */
    public static int audioOpen(int sampleRate, boolean is16Bit, boolean isStereo, int desiredFrames) {
        if (mPlaybackAudioService != null && mPlaybackAudioService.mExitThread) {
            Log.i(TAG, "audioOpen, status error, mPlaybackAudioService must be null when audioOpen.");
            return -1;
        }
        if (sampleRate < 8000 || sampleRate > 48000 || desiredFrames == 0 || ((desiredFrames & 0x3ff) != 0)) {
            Log.i(TAG, "audioOpen, invalid parameter.");
            return -1;
        }

        Log.i(TAG, "audioOpen, begin to create PlaybackAudio Service.");
        mPlaybackAudioService.audioSetParam(sampleRate, is16Bit, isStereo, desiredFrames);
        Intent playbackAudioServiceIntent = new Intent(mSingleton.getApplicationContext(), SDLService.class);
        mSingleton.bindService(playbackAudioServiceIntent, mSingleton.mPlaybackAudioServiceConnection, BIND_AUTO_CREATE);
        if (mPlaybackAudioService.mExitThread) {
            mPlaybackAudioService.mExitThread = false;
            mSingleton.startService(playbackAudioServiceIntent);
        }

        return 0;
    }

    /**
     * This method is called by SDL using JNI.
     */
    public static void audioClose() {
        // if (mPlaybackAudioService != null) {
        if (false) {
            mPlaybackAudioService.mDestroyed = false;

            Log.i(TAG, "audioQuit, will call unbindService.");
            mSingleton.unbindService(mSingleton.mPlaybackAudioServiceConnection);
            Log.i(TAG, "audioQuit, will call mPlaybackAudioService.audioQuit.");
            mPlaybackAudioService.audioQuit();


/*
            // don't block untial mPlaybackAudioService is destroyed. it will result to deal-lock.
            // tow factor effect serivce destroy.
            // 1: bind/unbind. they are also asynchronous between caller and service.
            // 2: work thread.
            while (!mPlaybackAudioService.mDestroyed) {
                try {
                    Thread.sleep(1);
                } catch (InterruptedException e) {
                    // Nom nom
                }
            }
*/
            mPlaybackAudioService = null;
            Log.i(TAG, "audioClose, set mPlaybackAudioService to null.");
        } else {
            Log.i(TAG, "audioClose, mPlaybackAudioService is null, do nothing.");
        }
    }

    /**
     * This method is called by SDL using JNI.
     */
    public static int captureOpen(int sampleRate, boolean is16Bit, boolean isStereo, int desiredFrames) {
        if (mPlaybackAudioService == null) {
            Log.i(TAG, "captureOpen, status error, mPlaybackAudioService must be valid when captureOpen.");
            return -1;
        }
        if (mPlaybackAudioService.isAudioCapturing()) {
            Log.i(TAG, "captureOpen, status error, current is capturing audio.");
            return -1;
        }
        if (sampleRate < 8000 || sampleRate > 48000 || !is16Bit || desiredFrames == 0 || ((desiredFrames & 0x1ff) != 0)) {
            // must be 16bit when capture.
            Log.i(TAG, "captureOpen, invalid parameter.");
            return -1;
        }

        Log.i(TAG, "captureOpen, begin to audioCaptureInit for capture.");
        mPlaybackAudioService.audioCaptureInit(sampleRate, is16Bit, isStereo, desiredFrames);
/*
        int channelConfig = isStereo ? AudioFormat.CHANNEL_CONFIGURATION_STEREO : AudioFormat.CHANNEL_CONFIGURATION_MONO;
        int audioFormat = is16Bit ? AudioFormat.ENCODING_PCM_16BIT : AudioFormat.ENCODING_PCM_8BIT;
        int frameSize = (isStereo ? 2 : 1) * (is16Bit ? 2 : 1);

        Log.v(TAG, "SDL capture: wanted " + (isStereo ? "stereo" : "mono") + " " + (is16Bit ? "16-bit" : "8-bit") + " " + (sampleRate / 1000f) + "kHz, " + desiredFrames + " frames buffer");

        // Let the user pick a larger buffer if they really want -- but ye
        // gods they probably shouldn't, the minimums are horrifyingly high
        // latency already
        desiredFrames = Math.max(desiredFrames, (AudioRecord.getMinBufferSize(sampleRate, channelConfig, audioFormat) + frameSize - 1) / frameSize);

        if (mAudioRecord == null) {
            mAudioRecord = new AudioRecord(MediaRecorder.AudioSource.DEFAULT, sampleRate,
                    channelConfig, audioFormat, desiredFrames * frameSize);

            // see notes about AudioTrack state in audioOpen(), above. Probably also applies here.
            if (mAudioRecord.getState() != AudioRecord.STATE_INITIALIZED) {
                Log.e(TAG, "Failed during initialization of AudioRecord");
                mAudioRecord.release();
                mAudioRecord = null;
                return -1;
            }

            mAudioRecord.startRecording();
        }

        Log.v(TAG, "SDL capture: got " + ((mAudioRecord.getChannelCount() >= 2) ? "stereo" : "mono") + " " + ((mAudioRecord.getAudioFormat() == AudioFormat.ENCODING_PCM_16BIT) ? "16-bit" : "8-bit") + " " + (mAudioRecord.getSampleRate() / 1000f) + "kHz, " + desiredFrames + " frames buffer");
*/
        return 0;
    }

    /** This method is called by SDL using JNI. */
    public static int captureReadShortBuffer(short[] buffer, boolean blocking) {
        // !!! FIXME: this is available in API Level 23. Until then, we always block.  :(
        //return mAudioRecord.read(buffer, 0, buffer.length, blocking ? AudioRecord.READ_BLOCKING : AudioRecord.READ_NON_BLOCKING);
        return mAudioRecord.read(buffer, 0, buffer.length);
    }

    /** This method is called by SDL using JNI. */
    public static int captureReadByteBuffer(byte[] buffer, boolean blocking) {
        // !!! FIXME: this is available in API Level 23. Until then, we always block.  :(
        //return mAudioRecord.read(buffer, 0, buffer.length, blocking ? AudioRecord.READ_BLOCKING : AudioRecord.READ_NON_BLOCKING);
        return mAudioRecord.read(buffer, 0, buffer.length);
    }

    /** This method is called by SDL using JNI. */
    public static void captureClose() {
        mPlaybackAudioService.audioCaptureDispose();
/*
        if (mAudioRecord != null) {
            mAudioRecord.stop();
            mAudioRecord.release();
            mAudioRecord = null;
        }
*/
    }

    // Input

    /**
     * This method is called by SDL using JNI.
     * @return an array which may be empty but is never null.
     */
    public static int[] inputGetInputDeviceIds(int sources) {
        int[] ids = InputDevice.getDeviceIds();
        int[] filtered = new int[ids.length];
        int used = 0;
        for (int i = 0; i < ids.length; ++i) {
            InputDevice device = InputDevice.getDevice(ids[i]);
            if ((device != null) && ((device.getSources() & sources) != 0)) {
                filtered[used++] = device.getId();
            }
        }
        return Arrays.copyOf(filtered, used);
    }

    // APK expansion files support

    /** com.android.vending.expansion.zipfile.ZipResourceFile object or null. */
    private static Object expansionFile;

    /** com.android.vending.expansion.zipfile.ZipResourceFile's getInputStream() or null. */
    private static Method expansionFileMethod;

    /**
     * This method is called by SDL using JNI.
     * @return an InputStream on success or null if no expansion file was used.
     * @throws IOException on errors. Message is set for the SDL error message.
     */
    public static InputStream openAPKExpansionInputStream(String fileName) throws IOException {
        // Get a ZipResourceFile representing a merger of both the main and patch files
        if (expansionFile == null) {
            String mainHint = nativeGetHint("SDL_ANDROID_APK_EXPANSION_MAIN_FILE_VERSION");
            if (mainHint == null) {
                return null; // no expansion use if no main version was set
            }
            String patchHint = nativeGetHint("SDL_ANDROID_APK_EXPANSION_PATCH_FILE_VERSION");
            if (patchHint == null) {
                return null; // no expansion use if no patch version was set
            }

            Integer mainVersion;
            Integer patchVersion;
            try {
                mainVersion = Integer.valueOf(mainHint);
                patchVersion = Integer.valueOf(patchHint);
            } catch (NumberFormatException ex) {
                ex.printStackTrace();
                throw new IOException("No valid file versions set for APK expansion files", ex);
            }

            try {
                // To avoid direct dependency on Google APK expansion library that is
                // not a part of Android SDK we access it using reflection
                expansionFile = Class.forName("com.android.vending.expansion.zipfile.APKExpansionSupport")
                    .getMethod("getAPKExpansionZipFile", Context.class, int.class, int.class)
                    .invoke(null, SDL.getContext(), mainVersion, patchVersion);

                expansionFileMethod = expansionFile.getClass()
                    .getMethod("getInputStream", String.class);
            } catch (Exception ex) {
                ex.printStackTrace();
                expansionFile = null;
                expansionFileMethod = null;
                throw new IOException("Could not access APK expansion support library", ex);
            }
        }

        // Get an input stream for a known file inside the expansion file ZIPs
        InputStream fileStream;
        try {
            fileStream = (InputStream)expansionFileMethod.invoke(expansionFile, fileName);
        } catch (Exception ex) {
            // calling "getInputStream" failed
            ex.printStackTrace();
            throw new IOException("Could not open stream from APK expansion file", ex);
        }

        if (fileStream == null) {
            // calling "getInputStream" was successful but null was returned
            throw new IOException("Could not find path in APK expansion file");
        }

        return fileStream;
    }

    // Messagebox

    /** Result of current messagebox. Also used for blocking the calling thread. */
    protected final int[] messageboxSelection = new int[1];

    /** Id of current dialog. */
    protected int dialogs = 0;

    /**
     * This method is called by SDL using JNI.
     * Shows the messagebox from UI thread and block calling thread.
     * buttonFlags, buttonIds and buttonTexts must have same length.
     * @param buttonFlags array containing flags for every button.
     * @param buttonIds array containing id for every button.
     * @param buttonTexts array containing text for every button.
     * @param colors null for default or array of length 5 containing colors.
     * @return button id or -1.
     */
    public int messageboxShowMessageBox(
            final int flags,
            final String title,
            final String message,
            final int[] buttonFlags,
            final int[] buttonIds,
            final String[] buttonTexts,
            final int[] colors) {

        messageboxSelection[0] = -1;

        // sanity checks

        if ((buttonFlags.length != buttonIds.length) && (buttonIds.length != buttonTexts.length)) {
            return -1; // implementation broken
        }

        // collect arguments for Dialog

        final Bundle args = new Bundle();
        args.putInt("flags", flags);
        args.putString("title", title);
        args.putString("message", message);
        args.putIntArray("buttonFlags", buttonFlags);
        args.putIntArray("buttonIds", buttonIds);
        args.putStringArray("buttonTexts", buttonTexts);
        args.putIntArray("colors", colors);

        // trigger Dialog creation on UI thread

        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                showDialog(dialogs++, args);
            }
        });

        // block the calling thread

        synchronized (messageboxSelection) {
            try {
                messageboxSelection.wait();
            } catch (InterruptedException ex) {
                ex.printStackTrace();
                return -1;
            }
        }

        // return selected value

        return messageboxSelection[0];
    }

    @Override
    protected Dialog onCreateDialog(int ignore, Bundle args) {

        // TODO set values from "flags" to messagebox dialog

        // get colors

        int[] colors = args.getIntArray("colors");
        int backgroundColor;
        int textColor;
        int buttonBorderColor;
        int buttonBackgroundColor;
        int buttonSelectedColor;
        if (colors != null) {
            int i = -1;
            backgroundColor = colors[++i];
            textColor = colors[++i];
            buttonBorderColor = colors[++i];
            buttonBackgroundColor = colors[++i];
            buttonSelectedColor = colors[++i];
        } else {
            backgroundColor = Color.TRANSPARENT;
            textColor = Color.TRANSPARENT;
            buttonBorderColor = Color.TRANSPARENT;
            buttonBackgroundColor = Color.TRANSPARENT;
            buttonSelectedColor = Color.TRANSPARENT;
        }

        // create dialog with title and a listener to wake up calling thread

        final Dialog dialog = new Dialog(this);
        dialog.setTitle(args.getString("title"));
        dialog.setCancelable(false);
        dialog.setOnDismissListener(new DialogInterface.OnDismissListener() {
            @Override
            public void onDismiss(DialogInterface unused) {
                synchronized (messageboxSelection) {
                    messageboxSelection.notify();
                }
            }
        });

        // create text

        TextView message = new TextView(this);
        message.setGravity(Gravity.CENTER);
        message.setText(args.getString("message"));
        if (textColor != Color.TRANSPARENT) {
            message.setTextColor(textColor);
        }

        // create buttons

        int[] buttonFlags = args.getIntArray("buttonFlags");
        int[] buttonIds = args.getIntArray("buttonIds");
        String[] buttonTexts = args.getStringArray("buttonTexts");

        final SparseArray<Button> mapping = new SparseArray<Button>();

        LinearLayout buttons = new LinearLayout(this);
        buttons.setOrientation(LinearLayout.HORIZONTAL);
        buttons.setGravity(Gravity.CENTER);
        for (int i = 0; i < buttonTexts.length; ++i) {
            Button button = new Button(this);
            final int id = buttonIds[i];
            button.setOnClickListener(new View.OnClickListener() {
                @Override
                public void onClick(View v) {
                    messageboxSelection[0] = id;
                    dialog.dismiss();
                }
            });
            if (buttonFlags[i] != 0) {
                // see SDL_messagebox.h
                if ((buttonFlags[i] & 0x00000001) != 0) {
                    mapping.put(KeyEvent.KEYCODE_ENTER, button);
                }
                if ((buttonFlags[i] & 0x00000002) != 0) {
                    mapping.put(KeyEvent.KEYCODE_ESCAPE, button); /* API 11 */
                }
            }
            button.setText(buttonTexts[i]);
            if (textColor != Color.TRANSPARENT) {
                button.setTextColor(textColor);
            }
            if (buttonBorderColor != Color.TRANSPARENT) {
                // TODO set color for border of messagebox button
            }
            if (buttonBackgroundColor != Color.TRANSPARENT) {
                Drawable drawable = button.getBackground();
                if (drawable == null) {
                    // setting the color this way removes the style
                    button.setBackgroundColor(buttonBackgroundColor);
                } else {
                    // setting the color this way keeps the style (gradient, padding, etc.)
                    drawable.setColorFilter(buttonBackgroundColor, PorterDuff.Mode.MULTIPLY);
                }
            }
            if (buttonSelectedColor != Color.TRANSPARENT) {
                // TODO set color for selected messagebox button
            }
            buttons.addView(button);
        }

        // create content

        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.addView(message);
        content.addView(buttons);
        if (backgroundColor != Color.TRANSPARENT) {
            content.setBackgroundColor(backgroundColor);
        }

        // add content to dialog and return

        dialog.setContentView(content);
        dialog.setOnKeyListener(new Dialog.OnKeyListener() {
            @Override
            public boolean onKey(DialogInterface d, int keyCode, KeyEvent event) {
                Button button = mapping.get(keyCode);
                if (button != null) {
                    if (event.getAction() == KeyEvent.ACTION_UP) {
                        button.performClick();
                    }
                    return true; // also for ignored actions
                }
                return false;
            }
        });

        return dialog;
    }

    private final Runnable rehideSystemUi = new Runnable() {
        @Override
        public void run() {
            int flags = View.SYSTEM_UI_FLAG_FULLSCREEN |
                    View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                    View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
                    View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
                    View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.INVISIBLE;

            SDLActivity.this.getWindow().getDecorView().setSystemUiVisibility(flags);
        }
    };

    public void onSystemUiVisibilityChange(int visibility) {
        if (SDLActivity.mFullscreenModeActive && (visibility & View.SYSTEM_UI_FLAG_FULLSCREEN) == 0 || (visibility & View.SYSTEM_UI_FLAG_HIDE_NAVIGATION) == 0) {

            Handler handler = getWindow().getDecorView().getHandler();
            if (handler != null) {
                handler.removeCallbacks(rehideSystemUi); // Prevent a hide loop.
                handler.postDelayed(rehideSystemUi, 2000);
            }

        }
    }

    /**
     * This method is called by SDL using JNI.
     */
    public static boolean clipboardHasText() {
        return mClipboardHandler.clipboardHasText();
    }
    
    /**
     * This method is called by SDL using JNI.
     */
    public static String clipboardGetText() {
        return mClipboardHandler.clipboardGetText();
    }

    /**
     * This method is called by SDL using JNI.
     */
    public static void clipboardSetText(String string) {
        mClipboardHandler.clipboardSetText(string);
    }
}

/**
    Simple runnable to start the SDL application
*/
class SDLMain implements Runnable {
    @Override
    public void run() {
        // Runs SDL_main()
        String library = SDLActivity.mSingleton.getMainSharedObject();
        String function = SDLActivity.mSingleton.getMainFunction();
        String[] arguments = SDLActivity.mSingleton.getArguments();

        Log.v("SDL", "Running main function " + function + " from library " + library);
        SDLActivity.nativeRunMain(library, function, arguments, Build.VERSION.SDK_INT);

        Log.v("SDL", "Finished main function");

        // Native thread has finished, let's finish the Activity
        if (!SDLActivity.mExitCalledFromJava) {
            SDLActivity.handleNativeExit();
        }
    }
}


/**
    SDLSurface. This is what we draw on, so we need to know when it's created
    in order to do anything useful.

    Because of this, that's where we set up the SDL thread
*/
class SDLSurface extends SurfaceView implements SurfaceHolder.Callback,
    View.OnKeyListener, View.OnTouchListener, SensorEventListener  {

    // Sensors
    protected static SensorManager mSensorManager;
    protected static Display mDisplay;

    // Keep track of the surface size to normalize touch events
    protected static float mWidth, mHeight;

    // Startup
    public SDLSurface(Context context) {
        super(context);
        getHolder().addCallback(this);

        setFocusable(true);
        setFocusableInTouchMode(true);
        requestFocus();
        setOnKeyListener(this);
        setOnTouchListener(this);

        mDisplay = ((WindowManager)context.getSystemService(Context.WINDOW_SERVICE)).getDefaultDisplay();
        mSensorManager = (SensorManager)context.getSystemService(Context.SENSOR_SERVICE);

        if (Build.VERSION.SDK_INT >= 12) {
            setOnGenericMotionListener(new SDLGenericMotionListener_API12());
        }

        // Some arbitrary defaults to avoid a potential division by zero
        mWidth = 1.0f;
        mHeight = 1.0f;
    }

    public void handlePause() {
        enableSensor(Sensor.TYPE_ACCELEROMETER, false);
    }

    public void handleResume() {
        setFocusable(true);
        setFocusableInTouchMode(true);
        requestFocus();
        setOnKeyListener(this);
        setOnTouchListener(this);
        enableSensor(Sensor.TYPE_ACCELEROMETER, true);
    }

    public Surface getNativeSurface() {
        return getHolder().getSurface();
    }

    // Called when we have a valid drawing surface
    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        Log.v("SDL", "surfaceCreated()");
        holder.setType(SurfaceHolder.SURFACE_TYPE_GPU);
    }

    // Called when we lose the surface
    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        Log.v("SDL", "surfaceDestroyed()");

        // Transition to pause, if needed
        SDLActivity.mNextNativeState = SDLActivity.NativeState.PAUSED;
        SDLActivity.handleNativeState();

        SDLActivity.mIsSurfaceReady = false;
        SDLActivity.onNativeSurfaceDestroyed();
    }

    // Called when the surface is resized
    @Override
    public void surfaceChanged(SurfaceHolder holder,
                               int format, int width, int height) {
        Log.v("SDL", "surfaceChanged()");

        int sdlFormat = 0x15151002; // SDL_PIXELFORMAT_RGB565 by default
        switch (format) {
        case PixelFormat.A_8:
            Log.v("SDL", "pixel format A_8");
            break;
        case PixelFormat.LA_88:
            Log.v("SDL", "pixel format LA_88");
            break;
        case PixelFormat.L_8:
            Log.v("SDL", "pixel format L_8");
            break;
        case PixelFormat.RGBA_4444:
            Log.v("SDL", "pixel format RGBA_4444");
            sdlFormat = 0x15421002; // SDL_PIXELFORMAT_RGBA4444
            break;
        case PixelFormat.RGBA_5551:
            Log.v("SDL", "pixel format RGBA_5551");
            sdlFormat = 0x15441002; // SDL_PIXELFORMAT_RGBA5551
            break;
        case PixelFormat.RGBA_8888:
            Log.v("SDL", "pixel format RGBA_8888");
            sdlFormat = 0x16462004; // SDL_PIXELFORMAT_RGBA8888
            break;
        case PixelFormat.RGBX_8888:
            Log.v("SDL", "pixel format RGBX_8888");
            sdlFormat = 0x16261804; // SDL_PIXELFORMAT_RGBX8888
            break;
        case PixelFormat.RGB_332:
            Log.v("SDL", "pixel format RGB_332");
            sdlFormat = 0x14110801; // SDL_PIXELFORMAT_RGB332
            break;
        case PixelFormat.RGB_565:
            Log.v("SDL", "pixel format RGB_565");
            sdlFormat = 0x15151002; // SDL_PIXELFORMAT_RGB565
            break;
        case PixelFormat.RGB_888:
            Log.v("SDL", "pixel format RGB_888");
            // Not sure this is right, maybe SDL_PIXELFORMAT_RGB24 instead?
            sdlFormat = 0x16161804; // SDL_PIXELFORMAT_RGB888
            break;
        default:
            Log.v("SDL", "pixel format unknown " + format);
            break;
        }

        mWidth = width;
        mHeight = height;
        SDLActivity.onNativeResize(width, height, sdlFormat, mDisplay.getRefreshRate());
        Log.v("SDL", "Window size: " + width + "x" + height);


        SDLActivity.mSingleton.mOrientationResult = false;
        boolean skip = false;
        int requestedOrientation = SDLActivity.mSingleton.getRequestedOrientation();

        if (requestedOrientation == ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED)
        {
            // Accept any
        }
        else if (requestedOrientation == ActivityInfo.SCREEN_ORIENTATION_PORTRAIT || requestedOrientation == ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT)
        {
            if (mWidth > mHeight) {
               skip = true;
            }
        } else if (requestedOrientation == ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE || requestedOrientation == ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE) {
            if (mWidth < mHeight) {
               skip = true;
            }
        }

        // Special Patch for Square Resolution: Black Berry Passport
        if (skip) {
           double min = Math.min(mWidth, mHeight);
           double max = Math.max(mWidth, mHeight);

           if (max / min < 1.20) {
              Log.v("SDL", "Don't skip on such aspect-ratio. Could be a square resolution.");
              skip = false;
           }
           skip = false;
        }

        if (skip) {
           Log.v("SDL", "Skip .. Surface is not ready.");
           SDLActivity.mIsSurfaceReady = false;
           return;
        }

        /* Surface is ready */
        SDLActivity.mIsSurfaceReady = true;

        /* If the surface has been previously destroyed by onNativeSurfaceDestroyed, recreate it here */
        SDLActivity.onNativeSurfaceChanged();

        SDLActivity.handleNativeState();
    }

    // Key events
    @Override
    public boolean onKey(View  v, int keyCode, KeyEvent event) {
        // Dispatch the different events depending on where they come from
        // Some SOURCE_JOYSTICK, SOURCE_DPAD or SOURCE_GAMEPAD are also SOURCE_KEYBOARD
        // So, we try to process them as JOYSTICK/DPAD/GAMEPAD events first, if that fails we try them as KEYBOARD
        //
        // Furthermore, it's possible a game controller has SOURCE_KEYBOARD and
        // SOURCE_JOYSTICK, while its key events arrive from the keyboard source
        // So, retrieve the device itself and check all of its sources
        if (SDLControllerManager.isDeviceSDLJoystick(event.getDeviceId())) {
            // Note that we process events with specific key codes here
            if (event.getAction() == KeyEvent.ACTION_DOWN) {
                if (SDLControllerManager.onNativePadDown(event.getDeviceId(), keyCode) == 0) {
                    return true;
                }
            } else if (event.getAction() == KeyEvent.ACTION_UP) {
                if (SDLControllerManager.onNativePadUp(event.getDeviceId(), keyCode) == 0) {
                    return true;
                }
            }
        }

        if ((event.getSource() & InputDevice.SOURCE_KEYBOARD) != 0) {
            if (event.getAction() == KeyEvent.ACTION_DOWN) {
                //Log.v("SDL", "key down: " + keyCode);
                if (SDLActivity.isTextInputEvent(event)) {
                    SDLInputConnection.nativeCommitText(String.valueOf((char) event.getUnicodeChar()), 1);
                }
                SDLActivity.onNativeKeyDown(keyCode);
                return true;
            }
            else if (event.getAction() == KeyEvent.ACTION_UP) {
                //Log.v("SDL", "key up: " + keyCode);
                SDLActivity.onNativeKeyUp(keyCode);
                return true;
            }
        }

        if ((event.getSource() & InputDevice.SOURCE_MOUSE) != 0) {
            // on some devices key events are sent for mouse BUTTON_BACK/FORWARD presses
            // they are ignored here because sending them as mouse input to SDL is messy
            if ((keyCode == KeyEvent.KEYCODE_BACK) || (keyCode == KeyEvent.KEYCODE_FORWARD)) {
                switch (event.getAction()) {
                case KeyEvent.ACTION_DOWN:
                case KeyEvent.ACTION_UP:
                    // mark the event as handled or it will be handled by system
                    // handling KEYCODE_BACK by system will call onBackPressed()
                    return true;
                }
            }
        }

        return false;
    }

    // Touch events
    @Override
    public boolean onTouch(View v, MotionEvent event) {
        /* Ref: http://developer.android.com/training/gestures/multi.html */
        final int touchDevId = event.getDeviceId();
        final int pointerCount = event.getPointerCount();
        int action = event.getActionMasked();
        int pointerFingerId;
        int mouseButton;
        int i = -1;
        float x,y,p;

        // !!! FIXME: dump this SDK check after 2.0.4 ships and require API14.
        if (event.getSource() == InputDevice.SOURCE_MOUSE && SDLActivity.mSeparateMouseAndTouch) {
            if (Build.VERSION.SDK_INT < 14) {
                mouseButton = 1; // all mouse buttons are the left button
            } else {
                try {
                    mouseButton = (Integer) event.getClass().getMethod("getButtonState").invoke(event);
                } catch(Exception e) {
                    mouseButton = 1;    // oh well.
                }
            }
            SDLActivity.onNativeMouse(mouseButton, action, event.getX(0), event.getY(0));
        } else {
            switch(action) {
                case MotionEvent.ACTION_MOVE:
                    for (i = 0; i < pointerCount; i++) {
                        pointerFingerId = event.getPointerId(i);
                        x = event.getX(i) / mWidth;
                        y = event.getY(i) / mHeight;
                        p = event.getPressure(i);
                        if (p > 1.0f) {
                            // may be larger than 1.0f on some devices
                            // see the documentation of getPressure(i)
                            p = 1.0f;
                        }
                        SDLActivity.onNativeTouch(touchDevId, pointerFingerId, action, x, y, p);
                    }
                    break;

                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_DOWN:
                    // Primary pointer up/down, the index is always zero
                    i = 0;
                case MotionEvent.ACTION_POINTER_UP:
                case MotionEvent.ACTION_POINTER_DOWN:
                    // Non primary pointer up/down
                    if (i == -1) {
                        i = event.getActionIndex();
                    }

                    pointerFingerId = event.getPointerId(i);
                    x = event.getX(i) / mWidth;
                    y = event.getY(i) / mHeight;
                    p = event.getPressure(i);
                    if (p > 1.0f) {
                        // may be larger than 1.0f on some devices
                        // see the documentation of getPressure(i)
                        p = 1.0f;
                    }
                    SDLActivity.onNativeTouch(touchDevId, pointerFingerId, action, x, y, p);
                    break;

                case MotionEvent.ACTION_CANCEL:
                    for (i = 0; i < pointerCount; i++) {
                        pointerFingerId = event.getPointerId(i);
                        x = event.getX(i) / mWidth;
                        y = event.getY(i) / mHeight;
                        p = event.getPressure(i);
                        if (p > 1.0f) {
                            // may be larger than 1.0f on some devices
                            // see the documentation of getPressure(i)
                            p = 1.0f;
                        }
                        SDLActivity.onNativeTouch(touchDevId, pointerFingerId, MotionEvent.ACTION_UP, x, y, p);
                    }
                    break;

                default:
                    break;
            }
        }

        return true;
   }

    // Sensor events
    public void enableSensor(int sensortype, boolean enabled) {
        // TODO: This uses getDefaultSensor - what if we have >1 accels?
        if (enabled) {
            mSensorManager.registerListener(this,
                            mSensorManager.getDefaultSensor(sensortype),
                            SensorManager.SENSOR_DELAY_GAME, null);
        } else {
            mSensorManager.unregisterListener(this,
                            mSensorManager.getDefaultSensor(sensortype));
        }
    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int accuracy) {
        // TODO
    }

    @Override
    public void onSensorChanged(SensorEvent event) {
        if (event.sensor.getType() == Sensor.TYPE_ACCELEROMETER) {
            float x, y;
            switch (mDisplay.getRotation()) {
                case Surface.ROTATION_90:
                    x = -event.values[1];
                    y = event.values[0];
                    break;
                case Surface.ROTATION_270:
                    x = event.values[1];
                    y = -event.values[0];
                    break;
                case Surface.ROTATION_180:
                    x = -event.values[1];
                    y = -event.values[0];
                    break;
                default:
                    x = event.values[0];
                    y = event.values[1];
                    break;
            }
            SDLActivity.onNativeAccel(-x / SensorManager.GRAVITY_EARTH,
                                      y / SensorManager.GRAVITY_EARTH,
                                      event.values[2] / SensorManager.GRAVITY_EARTH);
        }
    }
}

/* This is a fake invisible editor view that receives the input and defines the
 * pan&scan region
 */
class DummyEdit extends View implements View.OnKeyListener {
    InputConnection ic;

    public DummyEdit(Context context) {
        super(context);
        setFocusableInTouchMode(true);
        setFocusable(true);
        setOnKeyListener(this);
    }

    @Override
    public boolean onCheckIsTextEditor() {
        return true;
    }

    @Override
    public boolean onKey(View v, int keyCode, KeyEvent event) {
        /* 
         * This handles the hardware keyboard input
         */
        if (event.getAction() == KeyEvent.ACTION_DOWN) {
            if (SDLActivity.isTextInputEvent(event)) {
                ic.commitText(String.valueOf((char) event.getUnicodeChar()), 1);
                return true;
            }
            SDLActivity.onNativeKeyDown(keyCode);
            return true;
        } else if (event.getAction() == KeyEvent.ACTION_UP) {
            SDLActivity.onNativeKeyUp(keyCode);
            return true;
        }
        return false;
    }

    //
    @Override
    public boolean onKeyPreIme (int keyCode, KeyEvent event) {
        // As seen on StackOverflow: http://stackoverflow.com/questions/7634346/keyboard-hide-event
        // FIXME: Discussion at http://bugzilla.libsdl.org/show_bug.cgi?id=1639
        // FIXME: This is not a 100% effective solution to the problem of detecting if the keyboard is showing or not
        // FIXME: A more effective solution would be to assume our Layout to be RelativeLayout or LinearLayout
        // FIXME: And determine the keyboard presence doing this: http://stackoverflow.com/questions/2150078/how-to-check-visibility-of-software-keyboard-in-android
        // FIXME: An even more effective way would be if Android provided this out of the box, but where would the fun be in that :)
        if (event.getAction()==KeyEvent.ACTION_UP && keyCode == KeyEvent.KEYCODE_BACK) {
            if (SDLActivity.mTextEdit != null && SDLActivity.mTextEdit.getVisibility() == View.VISIBLE) {
                SDLActivity.onNativeKeyboardFocusLost();
            }
        }
        return super.onKeyPreIme(keyCode, event);
    }

    @Override
    public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
        ic = new SDLInputConnection(this, true);

        outAttrs.inputType = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD;
        outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI
                | EditorInfo.IME_FLAG_NO_FULLSCREEN /* API 11 */;

        return ic;
    }
}

class SDLInputConnection extends BaseInputConnection {

    public SDLInputConnection(View targetView, boolean fullEditor) {
        super(targetView, fullEditor);

    }

    @Override
    public boolean sendKeyEvent(KeyEvent event) {
        /*
         * This used to handle the keycodes from soft keyboard (and IME-translated input from hardkeyboard)
         * However, as of Ice Cream Sandwich and later, almost all soft keyboard doesn't generate key presses
         * and so we need to generate them ourselves in commitText.  To avoid duplicates on the handful of keys
         * that still do, we empty this out.
         */

        /*
         * Return DOES still generate a key event, however.  So rather than using it as the 'click a button' key
         * as we do with physical keyboards, let's just use it to hide the keyboard.
         */

        if (event.getKeyCode() == KeyEvent.KEYCODE_ENTER) {
            String imeHide = SDLActivity.nativeGetHint("SDL_RETURN_KEY_HIDES_IME");
            if ((imeHide != null) && imeHide.equals("1")) {
                Context c = SDL.getContext();
                if (c instanceof SDLActivity) {
                    SDLActivity activity = (SDLActivity)c;
                    activity.sendCommand(SDLActivity.COMMAND_TEXTEDIT_HIDE, null);
                    return true;
                }
            }
        }


        return super.sendKeyEvent(event);
    }

    @Override
    public boolean commitText(CharSequence text, int newCursorPosition) {

        for (int i = 0; i < text.length(); i++) {
            char c = text.charAt(i);
            nativeGenerateScancodeForUnichar(c);
        }

        SDLInputConnection.nativeCommitText(text.toString(), newCursorPosition);

        return super.commitText(text, newCursorPosition);
    }

    @Override
    public boolean setComposingText(CharSequence text, int newCursorPosition) {

        nativeSetComposingText(text.toString(), newCursorPosition);

        return super.setComposingText(text, newCursorPosition);
    }

    public static native void nativeCommitText(String text, int newCursorPosition);

    public native void nativeGenerateScancodeForUnichar(char c);

    public native void nativeSetComposingText(String text, int newCursorPosition);

    @Override
    public boolean deleteSurroundingText(int beforeLength, int afterLength) {
        // Workaround to capture backspace key. Ref: http://stackoverflow.com/questions/14560344/android-backspace-in-webview-baseinputconnection
        // and https://bugzilla.libsdl.org/show_bug.cgi?id=2265
        if (beforeLength > 0 && afterLength == 0) {
            boolean ret = true;
            // backspace(s)
            while (beforeLength-- > 0) {
               boolean ret_key = sendKeyEvent(new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_DEL))
                              && sendKeyEvent(new KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_DEL));
               ret = ret && ret_key; 
            }
            return ret;
        }

        return super.deleteSurroundingText(beforeLength, afterLength);
    }
}

interface SDLClipboardHandler {

    public boolean clipboardHasText();
    public String clipboardGetText();
    public void clipboardSetText(String string);

}


class SDLClipboardHandler_API11 implements
    SDLClipboardHandler, 
    android.content.ClipboardManager.OnPrimaryClipChangedListener {

    protected android.content.ClipboardManager mClipMgr;

    SDLClipboardHandler_API11() {
       mClipMgr = (android.content.ClipboardManager) SDL.getContext().getSystemService(Context.CLIPBOARD_SERVICE);
       mClipMgr.addPrimaryClipChangedListener(this);
    }

    @Override
    public boolean clipboardHasText() {
       return mClipMgr.hasText();
    }

    @Override
    public String clipboardGetText() {
        CharSequence text;
        text = mClipMgr.getText();
        if (text != null) {
           return text.toString();
        }
        return null;
    }

    @Override
    public void clipboardSetText(String string) {
       mClipMgr.removePrimaryClipChangedListener(this);
       mClipMgr.setText(string);
       mClipMgr.addPrimaryClipChangedListener(this);
    }
    
    @Override
    public void onPrimaryClipChanged() {
        SDLActivity.onNativeClipboardChanged();
    }

}

class SDLClipboardHandler_Old implements
    SDLClipboardHandler {
   
    protected android.text.ClipboardManager mClipMgrOld;
  
    SDLClipboardHandler_Old() {
       mClipMgrOld = (android.text.ClipboardManager) SDL.getContext().getSystemService(Context.CLIPBOARD_SERVICE);
    }

    @Override
    public boolean clipboardHasText() {
       return mClipMgrOld.hasText();
    }

    @Override
    public String clipboardGetText() {
       CharSequence text;
       text = mClipMgrOld.getText();
       if (text != null) {
          return text.toString();
       }
       return null;
    }

    @Override
    public void clipboardSetText(String string) {
       mClipMgrOld.setText(string);
    }
}

