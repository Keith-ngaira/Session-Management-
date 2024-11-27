#include <chrono>
#include <iostream>
#include <openssl/ssl.h>
#include <pjsua2.hpp>
#include <pj/config_site.h>
#include <pjsip.h>
#include <pjlib.h>
#include <pjlib-util.h>
#include <pjnath.h>
#include <pjsip_ua.h>
#include <pjsip_simple.h>
#include <pjsua-lib/pjsua.h>
#include <pjmedia.h>
#include <pjmedia-codec.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <csignal>

using namespace pj;
using namespace std;

// Global flag for application state
static bool isRunning = true;

// Derived Account Class
class MyAccount : public Account {
public:
    virtual void onRegState(OnRegStateParam& prm) override {
        AccountInfo ai = getInfo();
        if (ai.regIsActive) {
            cout << "[INFO] Successfully registered with the SIP server!" << endl;
        }
        else {
            cout << "[ERROR] Registration failed or inactive." << endl;
        }
    }

    virtual void onIncomingCall(OnIncomingCallParam& prm) override {
        Call* call = new Call(*this, prm.callId);
        CallOpParam callPrm;
        callPrm.statusCode = PJSIP_SC_OK;
        call->answer(callPrm);
        cout << "[INFO] Incoming call answered." << endl;
    }
};

// Derived Call Class
class MyCall : public Call {
public:
    MyCall(Account& acc) : Call(acc) {}

    virtual void onCallState(OnCallStateParam& prm) override {
        CallInfo ci = getInfo();
        cout << "[INFO] Call state changed: " << ci.stateText << endl;
        if (ci.state == PJSIP_INV_STATE_DISCONNECTED) {
            cout << "[INFO] Call disconnected." << endl;
            delete this;
        }
    }

    // Hold the call
    void holdCall() {
        CallOpParam prm;
        prm.opt.audioCount = 0;  // No audio stream (on hold)
        try {
            reinvite(prm);
            cout << "[INFO] Call put on hold." << endl;
        }
        catch (const Error& err) {
            cerr << "[ERROR] Failed to hold call: " << err.info() << endl;
        }
    }

    // Unhold the call
    void unholdCall() {
        CallOpParam prm;
        prm.opt.audioCount = 1;  // Restore audio streams
        try {
            reinvite(prm);
            cout << "[INFO] Call unheld." << endl;
        }
        catch (const Error& err) {
            cerr << "[ERROR] Failed to unhold call: " << err.info() << endl;
        }
    }

    // Mute the call
    void muteCall() {
        try {
            AudioMedia audioMedia = getAudioMedia(-1); // Default audio media
            audioMedia.adjustTxLevel(0.0);             // Mute transmission
            audioMedia.adjustRxLevel(0.0);             // Optionally mute receiving
            cout << "[INFO] Call muted." << endl;
        }
        catch (const Error& err) {
            cerr << "[ERROR] Failed to mute call: " << err.info() << endl;
        }
    }

    // Unmute the call
    void unmuteCall() {
        try {
            AudioMedia audioMedia = getAudioMedia(-1); // Default audio media
            audioMedia.adjustTxLevel(1.0);             // Restore transmission
            audioMedia.adjustRxLevel(1.0);             // Optionally unmute receiving
            cout << "[INFO] Call unmuted." << endl;
        }
        catch (const Error& err) {
            cerr << "[ERROR] Failed to unmute call: " << err.info() << endl;
        }
    }
};

// Initialize PJSIP
static void initPJSIP(Endpoint& ep) {
    EpConfig ep_cfg;
    ep_cfg.logConfig.level = 7; // Log level
    ep_cfg.logConfig.consoleLevel = 7;
    ep.libCreate();
    ep.libInit(ep_cfg);

    TransportConfig tcfg;
    tcfg.port = 5060; // Default SIP port
    ep.transportCreate(PJSIP_TRANSPORT_UDP, tcfg);

    ep.libStart();
    cout << "[INFO] PJSIP initialized and started." << endl;
}

// Register SIP Account
static MyAccount* registerAccount(const string& idUri, const string& registrarUri, const string& username, const string& password) {
    AccountConfig acc_cfg;
    acc_cfg.idUri = idUri;
    acc_cfg.regConfig.registrarUri = registrarUri;
    acc_cfg.regConfig.timeoutSec = 120;
    acc_cfg.sipConfig.authCreds.push_back(AuthCredInfo("digest", "*", username, 0, password));

    MyAccount* acc = new MyAccount();
    acc->create(acc_cfg);

    return acc;
}

// Keep account registration alive
static void startKeepAlive(MyAccount* acc) {
    if (!acc) return;  // Check for null account object

    thread keepAliveThread([acc]() {
        while (isRunning) {
            this_thread::sleep_for(chrono::seconds(5));
            try {
                if (acc) acc->setRegistration(true);
                cout << "[INFO] Account re-registered to keep it alive." << endl;
            }
            catch (const Error& err) {
                cerr << "[ERROR] Keep-alive registration failed: " << err.info() << endl;
            }
        }
        });
    keepAliveThread.detach();
}

// Make an outgoing call
static void makeCall(MyAccount* acc, const string& destination) {
    if (!acc) {
        cerr << "[ERROR] Cannot make call: Account is null." << endl;
        return;
    }

    MyCall* call = new MyCall(*acc);
    CallOpParam prm;
    prm.opt.audioCount = 1; // Audio call
    try {
        call->makeCall(destination, prm);
        cout << "[INFO] Outgoing call to " << destination << " initiated." << endl;
    }
    catch (const Error& err) {
        cerr << "[ERROR] Failed to make call: " << err.info() << endl;
        delete call;  // Clean up on failure
    }
}

// Signal handler for graceful shutdown
static void signalHandler(int signum) {
    cout << "[INFO] Signal received. Shutting down..." << endl;
    isRunning = false;
}

static void muteButtonHandler(MyCall* call) {
    char action;
    cout << "Press 'm' to mute or 'u' to unmute the call: ";
    cin >> action;
    if (action == 'm') {
        call->muteCall();
    }
    else if (action == 'u') {
        call->unmuteCall();
    }
}

static void holdButtonHandler(MyCall* call) {
    char action;
    cout << "Press 'h' to hold or 'r' to unhold the call: ";
    cin >> action;
    if (action == 'h') {
        call->holdCall();
    }
    else if (action == 'r') {
        call->unholdCall();
    }
}

int main() {
    signal(SIGINT, signalHandler);

    Endpoint ep;
    initPJSIP(ep);

    string idUri = "sip:5613@demo.dial-afrika.com";
    string registrarUri = "sip:demo.dial-afrika.com";
    string username = "5613";
    string password = "Temp@123";

    MyAccount* acc = registerAccount(idUri, registrarUri, username, password);

    startKeepAlive(acc);

    // Wait for a few seconds for registration to complete
    this_thread::sleep_for(chrono::seconds(10));

    // Make a call to another user
    MyCall* call = new MyCall(*acc);
    makeCall(acc, "sip:5614@demo.dial-afrika.com");

    // Wait for interaction
    while (isRunning) {
        muteButtonHandler(call);  // Handle mute/unmute
        holdButtonHandler(call);  // Handle hold/unhold
    }

    ep.libDestroy();

    return 0;
}
