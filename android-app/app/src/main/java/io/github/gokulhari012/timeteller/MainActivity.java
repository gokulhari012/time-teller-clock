package io.github.gokulhari012.timeteller;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.graphics.Insets;
import android.graphics.drawable.GradientDrawable;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.webkit.JsResult;
import android.webkit.RenderProcessGoneDetail;
import android.webkit.WebChromeClient;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.TextView;
import android.widget.Toast;
import android.window.OnBackInvokedCallback;
import android.window.OnBackInvokedDispatcher;

import java.io.IOException;
import java.net.HttpURLConnection;
import java.net.URI;
import java.net.URL;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * The whole app. The first screen shows the steps (join the clock's Wi-Fi, turn off mobile data)
 * and keeps checking whether the clock answers. "Open clock settings" then shows the clock's own
 * settings page (served by the ESP32 at http://192.168.4.1) in a WebView.
 */
public class MainActivity extends Activity {

    private static final int CHECK_EVERY_MS = 3000;    // look for the clock this often while it is not found
    private static final int CHECK_TIMEOUT_MS = 2500;  // wait this long for the clock to answer
    private static final int PAGE_TIMEOUT_MS = 15000;  // give up opening the settings page after this

    private enum Status { SEARCHING, FOUND, NO_WIFI, NOT_FOUND }

    private final Handler ui = new Handler(Looper.getMainLooper());
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final Runnable checkLater = this::checkNow;
    private final Runnable pageTimeout = this::pageFailed;

    private String clockUrl, clockAddress, wifiName;
    private View setupScreen, webScreen, pageLoading, statusBox, statusSpinner;
    private ViewGroup webHolder;
    private ImageView statusIcon;
    private TextView statusText;
    private Button openButton;
    private WebView web;              // made the first time the page is opened
    private AlertDialog leaveDialog;

    private ConnectivityManager connectivity;
    private ConnectivityManager.NetworkCallback wifiCallback;
    private volatile Network wifi;    // the Wi-Fi network the app's traffic goes through
    private volatile boolean closed;  // onDestroy has run

    private boolean resumed;          // the app is on screen
    private boolean checking;         // a check is running
    private boolean checkAgain;       // the Wi-Fi changed during that check, so its answer is out of date
    private boolean clockFound;       // the last check reached the clock
    private boolean openAfterCheck;   // "Open clock settings" is waiting for a check
    private boolean pageLoaded;       // the settings page loaded without an error
    private Object backCallback;      // Android 13+: the OnBackInvokedCallback used while the page is shown

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        clockUrl = getString(R.string.clock_url);
        clockAddress = Uri.parse(clockUrl).getAuthority();   // "192.168.4.1"
        wifiName = getString(R.string.wifi_name);

        setupScreen = findViewById(R.id.setup);
        webScreen = findViewById(R.id.webScreen);
        webHolder = findViewById(R.id.webHolder);
        pageLoading = findViewById(R.id.pageLoading);
        statusBox = findViewById(R.id.status);
        statusSpinner = findViewById(R.id.statusSpinner);
        statusIcon = findViewById(R.id.statusIcon);
        statusText = findViewById(R.id.statusText);
        openButton = findViewById(R.id.openButton);

        ((TextView) findViewById(R.id.address)).setText(getString(R.string.opens_address, clockAddress));
        findViewById(R.id.wifiButton).setOnClickListener(v -> openWifiSettings());
        findViewById(R.id.dataButton).setOnClickListener(v -> openMobileDataSettings());
        openButton.setOnClickListener(v -> openClockPage());

        fitBetweenSystemBars();
        showStatus(Status.SEARCHING);
        sendTrafficOverWifi();
    }

    @Override
    protected void onResume() {
        super.onResume();
        resumed = true;
        if (web != null) {
            web.onResume();
            web.resumeTimers();
        }
        if (isSetupShown()) checkNow();   // e.g. back from the Wi-Fi settings: look again straight away
        else if (!pageLoaded) ui.postDelayed(pageTimeout, PAGE_TIMEOUT_MS);   // the page was still loading
    }

    @Override
    protected void onPause() {
        resumed = false;
        ui.removeCallbacks(checkLater);
        ui.removeCallbacks(pageTimeout);   // a hidden page doesn't load, so don't count that time
        if (web != null) {
            web.onPause();
            web.pauseTimers();   // the page asks the clock for its status every 5 s; not while hidden
        }
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        closed = true;
        ui.removeCallbacksAndMessages(null);
        worker.shutdownNow();
        stopUsingWifi();
        if (web != null) {
            webHolder.removeView(web);
            web.destroy();
            web = null;
        }
        super.onDestroy();
    }

    // ---------------------------------------------------------------- first screen

    private void showStatus(Status status) {
        int text = R.string.status_searching, box = R.color.searching_bg, circle = 0, icon = 0;
        switch (status) {
            case FOUND:
                text = R.string.status_found;
                box = R.color.ok_bg;
                circle = R.color.ok;
                icon = R.drawable.ic_check;
                break;
            case NO_WIFI:
                text = R.string.status_no_wifi;
                box = R.color.warn_bg;
                circle = R.color.warn;
                icon = R.drawable.ic_alert;
                break;
            case NOT_FOUND:
                text = R.string.status_not_found;
                box = R.color.warn_bg;
                circle = R.color.warn;
                icon = R.drawable.ic_alert;
                break;
            default:
                break;
        }
        statusText.setText(status == Status.NOT_FOUND ? getString(text, wifiName) : getString(text));
        paint(statusBox, box);
        statusSpinner.setVisibility(icon == 0 ? View.VISIBLE : View.GONE);
        statusIcon.setVisibility(icon == 0 ? View.GONE : View.VISIBLE);
        if (icon != 0) {
            statusIcon.setImageResource(icon);
            paint(statusIcon, circle);
        }
    }

    private void openWifiSettings() {
        startFirstThatWorks(new Intent(Settings.ACTION_WIFI_SETTINGS),
                new Intent(Settings.ACTION_WIRELESS_SETTINGS),
                new Intent(Settings.ACTION_SETTINGS));
    }

    private void openMobileDataSettings() {
        Intent mobileNetwork = new Intent(Settings.ACTION_DATA_ROAMING_SETTINGS);
        Intent wireless = new Intent(Settings.ACTION_WIRELESS_SETTINGS);
        if (Build.VERSION.SDK_INT >= 29) {
            // Android 10+: a panel over the app with the mobile data switch
            startFirstThatWorks(new Intent(Settings.Panel.ACTION_INTERNET_CONNECTIVITY), mobileNetwork, wireless);
        } else {
            startFirstThatWorks(mobileNetwork, wireless, new Intent(Settings.ACTION_SETTINGS));
        }
    }

    /** Phones differ in which settings screens they have, so try each in turn. */
    private void startFirstThatWorks(Intent... intents) {
        for (Intent intent : intents) {
            try {
                // ForResult: the settings panels close at once if they can't tell which app opened them
                startActivityForResult(intent, 1);
                return;
            } catch (ActivityNotFoundException | SecurityException e) {
                // not on this phone: try the next one
            }
        }
        Toast.makeText(this, R.string.no_settings, Toast.LENGTH_LONG).show();
    }

    private void openClockPage() {
        if (clockFound) {
            showPage();
            return;
        }
        // Not found (yet): look once more, so a phone that isn't connected gets a clear message
        openAfterCheck = true;
        setOpenEnabled(false);
        showStatus(Status.SEARCHING);
        checkNow();
    }

    private void askOpenAnyway() {
        new AlertDialog.Builder(this)
                .setTitle(R.string.not_found_title)
                .setMessage(getString(R.string.not_found_message, wifiName))
                .setPositiveButton(R.string.try_again, (dialog, which) -> openClockPage())
                .setNegativeButton(R.string.open_anyway, (dialog, which) -> showPage())
                .show();
    }

    private void setOpenEnabled(boolean enabled) {
        openButton.setEnabled(enabled);
        openButton.setAlpha(enabled ? 1f : 0.5f);
    }

    // ---------------------------------------------------------------- is the clock there?

    /** Looks for the clock now, unless a look is already running. */
    private void checkNow() {
        ui.removeCallbacks(checkLater);
        if (checking || closed) return;
        checking = true;
        worker.execute(() -> {
            boolean found = false;
            try {
                found = clockAnswers();
            } finally {
                boolean answer = found;
                ui.post(() -> checkDone(answer));   // even if the check crashed, so it can't stay "checking"
            }
        });
    }

    private void checkDone(boolean found) {
        checking = false;
        if (closed) return;
        if (checkAgain) {   // the Wi-Fi changed while checking: ask again over the new one
            checkAgain = false;
            checkNow();
            return;
        }
        clockFound = found;
        if (openAfterCheck) {
            openAfterCheck = false;
            setOpenEnabled(true);
            if (found) {
                showPage();
                return;
            }
            askOpenAnyway();
        }
        if (isSetupShown()) {
            showStatus(found ? Status.FOUND : wifi == null ? Status.NO_WIFI : Status.NOT_FOUND);
            if (!found && resumed) ui.postDelayed(checkLater, CHECK_EVERY_MS);
        }
    }

    /** True if anything answers at the clock's address (runs on the worker thread). */
    private boolean clockAnswers() {
        HttpURLConnection connection = null;
        try {
            URL url = URI.create(clockUrl).toURL();
            Network network = wifi;
            connection = (HttpURLConnection) (network != null ? network.openConnection(url) : url.openConnection());
            connection.setConnectTimeout(CHECK_TIMEOUT_MS);
            connection.setReadTimeout(CHECK_TIMEOUT_MS);
            connection.setUseCaches(false);
            connection.setInstanceFollowRedirects(false);
            return connection.getResponseCode() > 0;
        } catch (IOException | RuntimeException e) {
            return false;
        } finally {
            if (connection != null) connection.disconnect();
        }
    }

    // ---------------------------------------------------------------- Wi-Fi

    /**
     * Sends all of the app's traffic (the checks and the settings page) through the Wi-Fi.
     * The clock's Wi-Fi has no internet, so while mobile data is on, Android would otherwise
     * try to reach 192.168.4.1 over mobile data, where the clock can't be found.
     */
    private void sendTrafficOverWifi() {
        connectivity = (ConnectivityManager) getSystemService(CONNECTIVITY_SERVICE);
        if (connectivity == null) return;
        NetworkRequest request = new NetworkRequest.Builder()
                .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)   // the clock's Wi-Fi has none
                .build();
        wifiCallback = new ConnectivityManager.NetworkCallback() {
            @Override
            public void onAvailable(Network network) {
                if (closed) return;
                wifi = network;
                useNetwork(network);
                ui.post(MainActivity.this::wifiChanged);
            }

            @Override
            public void onLost(Network network) {
                if (closed || !network.equals(wifi)) return;
                wifi = null;
                useNetwork(null);
                ui.post(MainActivity.this::wifiChanged);
            }
        };
        try {
            // A request (not just listening) also asks Android to keep this Wi-Fi connected
            connectivity.requestNetwork(request, wifiCallback);
        } catch (SecurityException e) {
            // Android 6.0.0 wrongly needed a special permission for requestNetwork
            connectivity.registerNetworkCallback(request, wifiCallback);
        }
    }

    @SuppressWarnings("deprecation")
    private void useNetwork(Network network) {
        if (Build.VERSION.SDK_INT >= 23) connectivity.bindProcessToNetwork(network);
        else ConnectivityManager.setProcessDefaultNetwork(network);
    }

    private void stopUsingWifi() {
        if (wifiCallback == null) return;
        try {
            connectivity.unregisterNetworkCallback(wifiCallback);
        } catch (IllegalArgumentException e) {
            // was not registered
        }
        useNetwork(null);
    }

    /** Wi-Fi connected or disconnected. */
    private void wifiChanged() {
        if (closed) return;
        clockFound = false;
        if (checking) {
            checkAgain = true;
        } else if (isSetupShown()) {
            showStatus(Status.SEARCHING);
            checkNow();
        }
    }

    // ---------------------------------------------------------------- the clock's settings page

    private void showPage() {
        if (!makeWebView()) return;
        ui.removeCallbacks(checkLater);
        setupScreen.setVisibility(View.GONE);
        webScreen.setVisibility(View.VISIBLE);
        pageLoading.setVisibility(View.VISIBLE);
        pageLoaded = false;
        handleBackWhilePageShown(true);
        web.loadUrl(clockUrl);
        ui.postDelayed(pageTimeout, PAGE_TIMEOUT_MS);
    }

    private void showSetup() {
        ui.removeCallbacks(pageTimeout);
        if (web != null) web.loadUrl("about:blank");   // stop the page asking the clock for its status
        webScreen.setVisibility(View.GONE);
        setupScreen.setVisibility(View.VISIBLE);
        handleBackWhilePageShown(false);
        checkNow();
    }

    /** The page did not load (no answer, wrong Wi-Fi, ...): back to the steps. */
    private void pageFailed() {
        if (!isPageShown() || pageLoaded) return;
        clockFound = false;
        showSetup();
        showStatus(Status.SEARCHING);
        Toast.makeText(this, R.string.page_failed, Toast.LENGTH_LONG).show();
    }

    /** Back while the page is shown: back to the steps, asking first if changes are not saved. */
    private void leavePage() {
        if (leaveDialog != null && leaveDialog.isShowing()) return;
        if (!pageLoaded) {
            showSetup();
            return;
        }
        // The page shows its "Unsaved changes" bar (id "bar", class "show") until the changes are saved
        web.evaluateJavascript(
                "(function(){var b=document.getElementById('bar');return !!(b&&b.classList.contains('show'));})()",
                unsaved -> {
                    if (closed || !isPageShown() || (leaveDialog != null && leaveDialog.isShowing())) return;
                    if (!"true".equals(unsaved)) {   // evaluateJavascript returns JSON: true / false
                        showSetup();
                        return;
                    }
                    leaveDialog = new AlertDialog.Builder(this)
                            .setMessage(R.string.unsaved_message)
                            .setPositiveButton(R.string.leave, (dialog, which) -> showSetup())
                            .setNegativeButton(R.string.stay, null)
                            .show();
                });
    }

    // Android 12 and older. Android 13+ never calls this (enableOnBackInvokedCallback is on in the
    // manifest); there, handleBackWhilePageShown registers a callback instead.
    @Override
    @SuppressLint("GestureBackNavigation")
    @SuppressWarnings("deprecation")
    public void onBackPressed() {
        if (isPageShown()) leavePage();
        else super.onBackPressed();
    }

    /** Android 13+: while the page is shown, back leaves the page instead of closing the app. */
    private void handleBackWhilePageShown(boolean pageShown) {
        if (Build.VERSION.SDK_INT < 33) return;   // onBackPressed does it
        OnBackInvokedDispatcher dispatcher = getOnBackInvokedDispatcher();
        if (pageShown && backCallback == null) {
            OnBackInvokedCallback callback = this::leavePage;
            dispatcher.registerOnBackInvokedCallback(OnBackInvokedDispatcher.PRIORITY_DEFAULT, callback);
            backCallback = callback;
        } else if (!pageShown && backCallback != null) {
            dispatcher.unregisterOnBackInvokedCallback((OnBackInvokedCallback) backCallback);
            backCallback = null;
        }
    }

    /** Makes the WebView the first time the page is opened (not at start, so the steps show at once). */
    @SuppressLint("SetJavaScriptEnabled")
    private boolean makeWebView() {
        if (web != null) return true;
        try {
            web = new WebView(this);
        } catch (RuntimeException e) {   // Android System WebView is missing or being updated
            Toast.makeText(this, R.string.no_webview, Toast.LENGTH_LONG).show();
            return false;
        }
        web.resumeTimers();   // timers are shared by all WebViews and may still be paused from before
        web.setBackgroundColor(color(R.color.bg));
        WebSettings settings = web.getSettings();
        settings.setJavaScriptEnabled(true);                // the settings page is a JavaScript app
        settings.setDomStorageEnabled(true);                // it remembers the password in localStorage
        settings.setCacheMode(WebSettings.LOAD_NO_CACHE);   // always the clock's current page
        settings.setAllowFileAccess(false);
        settings.setAllowContentAccess(false);
        web.setWebViewClient(new PageClient());
        web.setWebChromeClient(new PageDialogs());
        webHolder.addView(web, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        return true;
    }

    private boolean isClockPage(String url) {
        return url != null && clockAddress != null && clockAddress.equals(Uri.parse(url).getAuthority());
    }

    /** Keeps the clock's page in the app and notices when it fails to load. */
    private class PageClient extends WebViewClient {
        @Override
        @SuppressWarnings("deprecation")   // the WebResourceRequest version needs Android 7
        public boolean shouldOverrideUrlLoading(WebView view, String url) {
            if (isClockPage(url)) return false;
            try {
                startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url)));   // anything else: the phone's browser
            } catch (ActivityNotFoundException e) {
                // nothing on the phone can open it
            }
            return true;
        }

        @Override
        public void onPageFinished(WebView view, String url) {
            if (!isClockPage(url) || !isPageShown()) return;
            pageLoaded = true;
            ui.removeCallbacks(pageTimeout);
            pageLoading.setVisibility(View.GONE);
        }

        @Override
        @SuppressWarnings("deprecation")   // still called on new Android versions, for the page itself
        public void onReceivedError(WebView view, int errorCode, String description, String failingUrl) {
            if (isClockPage(failingUrl)) pageFailed();
        }

        @Override
        public boolean onRenderProcessGone(WebView view, RenderProcessGoneDetail detail) {
            recreate();   // the page's renderer crashed or was stopped to free memory: start again
            return true;
        }
    }

    /** Shows the page's confirm() and alert() boxes as normal Android dialogs. */
    private class PageDialogs extends WebChromeClient {
        @Override
        public boolean onJsConfirm(WebView view, String url, String message, JsResult result) {
            new AlertDialog.Builder(MainActivity.this)
                    .setMessage(message)
                    .setPositiveButton(android.R.string.ok, (dialog, which) -> result.confirm())
                    .setNegativeButton(android.R.string.cancel, (dialog, which) -> result.cancel())
                    .setOnCancelListener(dialog -> result.cancel())
                    .show();
            return true;
        }

        @Override
        public boolean onJsAlert(WebView view, String url, String message, JsResult result) {
            new AlertDialog.Builder(MainActivity.this)
                    .setMessage(message)
                    .setPositiveButton(android.R.string.ok, (dialog, which) -> result.confirm())
                    .setOnCancelListener(dialog -> result.confirm())
                    .show();
            return true;
        }

        @Override
        public boolean onJsBeforeUnload(WebView view, String url, String message, JsResult result) {
            result.confirm();   // the app has already asked about unsaved changes (see leavePage)
            return true;
        }
    }

    // ---------------------------------------------------------------- layout helpers

    private boolean isSetupShown() {
        return setupScreen.getVisibility() == View.VISIBLE;
    }

    private boolean isPageShown() {
        return webScreen.getVisibility() == View.VISIBLE;
    }

    /**
     * Android 11+: the app draws behind the status and navigation bars (Android 15+ always does),
     * so keep the screens clear of them and of the keyboard, and colour the strip behind the status
     * bar like the page's header. Older versions do this themselves (see themes.xml).
     */
    @SuppressWarnings("deprecation")   // setDecorFitsSystemWindows: Android 15+ does it anyway, 11-14 need it
    private void fitBetweenSystemBars() {
        if (Build.VERSION.SDK_INT < 30) return;
        getWindow().setDecorFitsSystemWindows(false);
        View statusBarBackground = findViewById(R.id.statusBarBackground);
        View screens = findViewById(R.id.screens);
        findViewById(R.id.root).setOnApplyWindowInsetsListener((root, insets) -> {
            Insets bars = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            Insets keyboard = insets.getInsets(WindowInsets.Type.ime());
            ViewGroup.LayoutParams params = statusBarBackground.getLayoutParams();
            params.height = bars.top;
            statusBarBackground.setLayoutParams(params);
            screens.setPadding(bars.left, 0, bars.right, Math.max(bars.bottom, keyboard.bottom));
            return WindowInsets.CONSUMED;
        });
    }

    /** Sets the fill colour of a view's shape background. */
    private void paint(View view, int colorId) {
        ((GradientDrawable) view.getBackground().mutate()).setColor(color(colorId));
    }

    @SuppressWarnings("deprecation")
    private int color(int colorId) {
        return getResources().getColor(colorId);
    }
}
