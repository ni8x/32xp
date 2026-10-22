#!/usr/bin/env python3
"""Static checks for the fixed-IP feature (run: python tools/check_net.py).

No compiler is needed. Pinning the dashboard's address is the one setting that
can make the device unreachable, so this script pins the guard rails:

  * the setting round-trips (getter/setter, NVS load + save, defaults everywhere),
  * the address is validated BEFORE it is stored - parse, unicast sanity, subnet
    membership, gateway, network/broadcast - and a rejected value changes nothing,
  * DHCP is the boot path, and a pin is only applied after the lease shows the
    device is on the pin's network - a pin from somewhere else associates and
    then routes nowhere, which is unreachable rather than merely misconfigured,
  * a stale pin is kept (it may apply again later) but skipped, and a link drop
    releases the pin so the next association re-learns the network,
  * the lease is learned from DHCP, rewritten only when it really changes, and
    the pin is judged against it,
  * the web surface is wired: route + 400s + restart, status fields, UI controls.

The validation rules are also table-tested here through a mirror of the C++
logic; the shipped lines are pinned by string so a rewrite invalidates the
mirror. The authoritative test is against the running board.
"""

import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
fails = []


def read(p):
    return io.open(os.path.join(ROOT, p), encoding='utf-8').read()


def check(label, ok, detail=''):
    print('%-4s %s%s' % ('ok' if ok else 'FAIL', label, (' - ' + detail) if detail and not ok else ''))
    if not ok:
        fails.append(label)


def brace_body(text, marker):
    i = text.index(marker)
    i = text.index('{', i) + 1
    depth, start = 1, i
    while depth:
        depth += (text[i] == '{') - (text[i] == '}')
        i += 1
    return text[start:i - 1]


def branch(text, marker):
    """Body of the first brace block after marker (for if/else-if branches)."""
    return brace_body(text, marker)


st_h = read('src/Settings.h')
st_cpp = read('src/Settings.cpp')
wifi_h = read('src/WiFiManager.h')
wifi = read('src/WiFiManager.cpp')
web = read('src/WebServer.cpp')

# ------------------------------------------------------------ settings round-trip

ctor = st_cpp[st_cpp.index('Settings::Settings()'):]
ctor_init = ctor[:ctor.index('{')]
clear_all = brace_body(st_cpp, 'void Settings::clearAll()')

# (accessor suffix, NVS key, member name) - the member keeps the readable name
# while the key stays short.
for name, key, member in (('StaticIp', 'staticIp', 'staticIp'),
                          ('NetGateway', 'netGw', 'netGateway'),
                          ('NetSubnet', 'netMask', 'netSubnet'),
                          ('NetDns', 'netDns', 'netDns')):
    check('setting %-10s has a getter and a setter' % key,
          ('String get%s();' % name) in st_h and ('void set%s(' % name) in st_h)
    check('setting %-10s is loaded from and saved to NVS' % key,
          ('getString("%s"' % key) in st_cpp and ('putString("%s"' % key) in st_cpp)
    check('setting %-10s is defaulted at construction and in clearAll()' % key,
          re.search(r'_%s\(\)' % member, ctor_init) is not None and
          re.search(r'_%s = "";' % member, clear_all) is not None)

# The lease fields are written from the live network on every reconnect, so an
# unchanged value must not reach NVS.
check('lease setters skip an unchanged value (no flash churn)',
      len(re.findall(r'if \(_\w+ == \w+\) return;', st_cpp)) >= 3,
      str(len(re.findall(r'if \(_\w+ == \w+\) return;', st_cpp))))

# ------------------------------------------------------------- validation rules

setter = brace_body(wifi, 'bool WiFiManager::setStaticIp')
check('the empty value is the DHCP path, and it clears the stored address',
      'if (value.isEmpty())' in setter and '_settings.setStaticIp("")' in setter)
check('an address is parsed before anything is stored',
      setter.index('parseIp(value, parsed)') < setter.index('_settings.setStaticIp(value)'))
check('a rejected address returns before the store',
      setter.count('return false;') >= 5 and setter.index('return false;') <
      setter.index('_settings.setStaticIp(value)'))
check('non-unicast addresses are refused',
      'usableUnicast(parsed)' in setter and
      'if (a == 0 || a == 127 || a >= 224) return false;' in wifi and
      'ip[0] == 255 && ip[1] == 255 && ip[2] == 255 && ip[3] == 255' in wifi)
check('an address outside the learned subnet is refused (the lockout case)',
      'if (parseIp(_settings.getNetGateway(), gateway) && parseIp(_settings.getNetSubnet(), mask)) {'
      in setter and 'if (!sameSubnet(parsed, gateway, mask)) {' in setter and
      setter.index('sameSubnet(parsed, gateway, mask)') <
      setter.index('_settings.setStaticIp(value)') and
      'if ((uint8_t)(a[i] & mask[i]) != (uint8_t)(b[i] & mask[i])) return false;' in wifi,
      'the guard must be a live condition, not a disabled one')
check('the gateway and the network/broadcast addresses are refused',
      'parsed == gateway' in setter and 'hostBitsZero' in setter and 'hostBitsAll' in setter)
check('the subnet check tolerates a mask that is not /24',
      'const uint8_t width = (uint8_t)(~mask[i] & 0xFF);' in setter)

# Mirror of the shipped rules, so the decisions themselves are table-tested.
def parse_ok(text):
    parts = text.split('.')
    if len(parts) != 4 or not all(p.isdigit() and 0 <= int(p) <= 255 and p != '' for p in parts):
        return None
    if any(len(p) > 3 for p in parts):
        return None
    return tuple(int(p) for p in parts)


def usable(ip):
    if ip[0] == 0 or ip[0] == 127 or ip[0] >= 224:
        return False
    if ip == (255, 255, 255, 255):
        return False
    return True


def accepts(text, gateway=None, mask=None):
    ip = parse_ok(text)
    if ip is None or not usable(ip):
        return False
    if not gateway or not mask:
        return True                      # nothing learned yet: no subnet guard
    gw, mk = tuple(gateway), tuple(mask)
    if any((ip[i] & mk[i]) != (gw[i] & mk[i]) for i in range(4)):
        return False
    if ip == gw:
        return False
    if all((ip[i] & (255 - mk[i])) == 0 for i in range(4)):
        return False                     # network address
    if all((ip[i] & (255 - mk[i])) == (255 - mk[i]) for i in range(4)):
        return False                     # broadcast address
    return True


LAN24 = ((10, 122, 44, 1), (255, 255, 255, 0))
LAN16 = ((192, 168, 4, 1), (255, 255, 0, 0))
cases = [
    ('10.122.44.230', LAN24, True, 'a free host on the current /24'),
    ('10.122.44.1', LAN24, False, 'the gateway itself'),
    ('10.122.44.0', LAN24, False, 'the network address'),
    ('10.122.44.255', LAN24, False, 'the broadcast address'),
    ('10.122.45.5', LAN24, False, 'another subnet'),
    ('10.122.44.230', None, True, 'no lease learned yet'),
    ('192.168.7.9', LAN16, True, 'a /16 network is not treated as /24'),
    ('192.168.4.1', LAN16, False, 'the /16 gateway'),
    ('0.0.0.0', LAN24, False, 'the DHCP placeholder'),
    ('127.0.0.1', LAN24, False, 'loopback'),
    ('224.0.0.1', LAN24, False, 'multicast'),
    ('255.255.255.255', LAN24, False, 'broadcast'),
    ('10.122.44', LAN24, False, 'too few octets'),
    ('10.122.44.230.1', LAN24, False, 'too many octets'),
    ('10.122.44.256', LAN24, False, 'octet out of range'),
    ('10.122.44.x', LAN24, False, 'not a number'),
    ('', LAN24, False, 'empty parses as nothing (DHCP handled before this)'),
]
bad = [(t, why) for t, lan, want, why in cases
       if accepts(t, *(lan or (None, None))) != want]
check('validation table (pinned mirror): %d cases' % len(cases), not bad,
      str(bad))

# ----------------------------------------------------------- application path

connect = brace_body(wifi, 'void WiFiManager::connect')
consider = brace_body(wifi, 'void WiFiManager::considerPin')
release = brace_body(wifi, 'void WiFiManager::releasePin')
update = brace_body(wifi, 'void WiFiManager::update')

# DHCP is the boot path, unconditionally. A pinned address used to be applied
# before begin(), which stranded the board on a network it had left: it
# associated, held a foreign address and answered nothing.
check('every connection starts on DHCP', 'WiFi.config(' not in connect and
      connect.index('WiFi.begin(') > connect.index('_pinConsidered = false;'))
check('the lease is known before a pin is judged',
      update.index('learnLease();') < update.index('considerPin();'),
      'considerPin() must run after the lease on the same connection')
check('the pin is judged once per connection, not once per loop',
      'if (_pinConsidered || _staticApplied) return;' in consider)
check('a pin from another network is never applied',
      'sameSubnet(pin, gateway, mask)' in consider and
      'WiFi.config(' in consider and
      consider.count('WiFi.config(') == 1 and
      consider.index('sameSubnet(pin, gateway, mask)') < consider.index('WiFi.config('),
      'the subnet test must be the only gate before the single config() call')
check('a skipped pin is kept, not deleted',
      'setStaticIp("")' not in consider and 'setStaticIp("")' not in release,
      'the value still applies if the board goes back to that network')
check('a skipped pin is logged with both networks',
      'is not on this network' in consider and 'WiFi.localIP().toString()' in consider)
check('the applied pin is reported with the address DHCP had',
      'DHCP had' in consider)
check('gateway/mask are read from the live interface, not from storage',
      'WiFi.gatewayIP()' in consider and 'WiFi.subnetMask()' in consider and
      '_settings.getNetGateway()' not in consider,
      'a stored lease can be from the old network too')
check('the lease not being readable yet defers the decision',
      'return;    // lease not readable yet' in consider,
      'the decision must be retried on a later tick, not skipped for the boot')

# Losing the link must also lose the pin, or the next association reuses an
# address from the network that just went away.
drop_branch = branch(update, 'else if (!currentlyConnected && _wasConnected)')
check('a disconnect releases the pin back to DHCP',
      re.search(r'(?m)^\s{8}releasePin\(\);$', drop_branch) is not None,
      'an uncommented call in the disconnect branch, not merely a mention of one')
check('the release restarts the DHCP client',
      'WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));' in release,
      'config() with an empty address is the way back to DHCP on this core')
check('the release also forgets the judgement so the pin is re-judged',
      release.index('_pinConsidered = false;') < release.index('if (!_staticApplied) return;'))

state = brace_body(wifi, 'String WiFiManager::staticIpState')
check('the pin state is published as a word the page can map',
      '"active"' in state and '"off-network"' in state and '"dhcp"' in state)

learn = brace_body(wifi, 'void WiFiManager::learnLease')
check('the lease is learned when a connection is established',
      'learnLease();' in update)
check('the address is still logged on connect and on change',
      'logAddress("IP address:")' in update and 'logAddress("New IP address:")' in update)

# ---------------------------------------------------------------- web surface

route = brace_body(web, '_server.on("/api/staticip"')
check('the fixed-IP route exists and is a POST', '/api/staticip' in web)
check('a missing parameter is a 400', 'Missing ip parameter' in route and 'send(400' in route)
check('a rejected address is a 400 carrying the reason',
      'send(400, "text/plain", error)' in route)
check('a stored address is answered, then the restart is scheduled',
      route.index('send(200') < route.index('scheduleRestart();') and
      'ESP.restart' not in route,
      'restarting inside the handler ate the reply the page was waiting for')
app_cpp = read('src/App.cpp')
restart_body = brace_body(web, 'void WebServer::update')
check('the scheduled restart runs from the loop',
      '_webServer.update();' in app_cpp and 'ESP.restart();' in restart_body)
check('every restarting route defers through the same helper',
      web.count('scheduleRestart();') == 3 and 'ESP.restart' not in
      re.sub(r'void WebServer::update.*', '', web, flags=re.S),
      'staticip, restart and reset - no inline restart left behind')
check('the manager owns validation, not the route',
      'setStaticIp(request->getParam("ip", true)->value(), error)' in route)

for field in ('staticIp', 'gateway', 'subnet'):
    check('status publishes %s' % field, 'doc["%s"]' % field in web)
check('status publishes the pin state from the manager',
      'doc["staticIpState"] = _wifiManager.staticIpState();' in web)
check('every address field is read by the page script',
      web.count("document.getElementById('ip')") >= 1 and
      "data.gateway" in web and "data.subnet" in web and "data.staticIp" in web)
check('the fixed-IP control has a field and both actions',
      'id="staticIp"' in web and 'function saveStaticIp()' in web and
      'function clearStaticIp()' in web)

# Every id the script reads must exist in the shell markup, and the new control
# must actually be read - the check_plugins.py rule, applied to the shell.
shell = web.split('R"rawliteral(', 1)[1].split(')rawliteral', 1)[0]
script = shell.split('<script>', 1)[1].rsplit('</script>', 1)[0]
ids = set(re.findall(r'id="([^"]+)"', shell))
refs = set(re.findall(r"getElementById\('([^']+)'\)", script))
new_refs = set(r for r in refs if r in ('staticIp', 'gw2', 'subnet2', 'staticState2'))
check('the shell script only reads ids that exist in the shell markup',
      refs <= ids, 'missing: ' + str(sorted(refs - ids)))
check('the fixed-IP controls are read by the shell script',
      new_refs == {'staticIp', 'gw2', 'subnet2', 'staticState2'},
      'read: ' + str(sorted(new_refs)))
check('a skipped pin is visible on the page, not just in the log',
      "'off-network': 'No - not on this network'" in web and
      "data.staticIpState" in web)

# The client-side guard mirrors the device rule only as a courtesy; the device is
# the authority, so the regex must stay loose enough to let real addresses through
# and the server decides.
check('the page validates the shape before posting, but the device decides',
      r"/^\d{1,3}(\.\d{1,3}){3}$/" in web and "postData('/api/staticip', 'ip='" in web)

# ------------------------------------------------------- connectivity truth
#
# The dashboard reported "Disconnected" (and 0.0.0.0) on a board that was
# serving the page: on this core WL_CONNECTED is only latched by the GOT_IP
# event, and a station with a pinned address can be up and addressed without it.
# Connectivity therefore has to come from the interface owning an address, which
# is also what the panel, the scores feeds and the timestamp clock rely on.

connected_body = brace_body(wifi, 'bool WiFiManager::isConnected')
check('connectivity is decided by the interface address as well as the flag',
      'if (WiFi.status() == WL_CONNECTED) return true;' in connected_body and
      'return hasAddress() && WiFi.RSSI() != 0;' in connected_body,
      'the flag can be stale, and a pinned address survives a dropped link')
check('a live association is required before the address is believed',
      connected_body.index('hasAddress()') < connected_body.index('WiFi.RSSI() != 0'),
      'RSSI() reads the AP actually associated with; 0 means none')
check('an address means a non-zero local IP',
      'WiFi.localIP() != IPAddress(0, 0, 0, 0)' in brace_body(wifi, 'bool WiFiManager::hasAddress'))
ip_body = brace_body(wifi, 'String WiFiManager::ipAddress')
check('the address is published whenever the interface owns one',
      'if (WiFi.localIP() != IPAddress(0, 0, 0, 0)) {' in ip_body and
      'return WiFi.localIP().toString();' in ip_body and
      'if (_apMode) return WiFi.softAPIP().toString();' in ip_body)
check('first boot without credentials starts a setup hotspot',
      'if (_settings.getWifiSSID().isEmpty()) {' in wifi and
      'startSetupAp();' in wifi and
      'WiFi.softAP(SETUP_AP_SSID, SETUP_AP_PASS);' in wifi)
check('the setup hotspot survives while the station joins home Wi-Fi',
      'if (_apMode) {' in brace_body(wifi, 'void WiFiManager::connect') and
      'WiFi.mode(WIFI_AP_STA);' in wifi)
check('the human status says Connected for an addressed station',
      'if (isConnected()) return "Connected";' in brace_body(wifi, 'String WiFiManager::getStatusString'))
check('the driver\'s own word stays available for diagnosis',
      'String WiFiManager::getRawStatusString' in wifi and
      'case WL_IDLE_STATUS: return "Idle";' in wifi)
check('the reconnect timer runs only when there is no address to serve on',
      'if (!hasAddress()) {' in update and
      'bool currentlyConnected = isConnected();' in update,
      're-begin()ing a working link churns it without ever latching the flag')
check('the disagreement is logged, rate limited',
      'WiFi.status() != WL_CONNECTED' in update and '_lastFlagWarnAt' in update and
      '>= 60000' in update)

# Mirror of the shipped derivation: (driver flag, address, RSSI) -> connected.
# The RSSI row comes from the board: after a disconnect the pinned address is
# still there (10.122.44.230) while RSSI() reads 0 - the address alone would
# have called a dead link connected.
def usable_link(flag_connected, ip, rssi):
    return flag_connected or (ip != (0, 0, 0, 0) and rssi != 0)


link_cases = [
    (True, (10, 122, 44, 230), -46, True, 'the ordinary connected case'),
    (False, (10, 122, 44, 230), -46, True, 'flag never latched, association alive'),
    (True, (0, 0, 0, 0), 0, True, 'flag latched a moment before the address is readable'),
    (False, (10, 122, 44, 230), 0, False, 'link dropped, pinned address retained'),
    (False, (0, 0, 0, 0), 0, False, 'no address and no flag: a real disconnect'),
]
bad = [(c, why) for c, ip, rssi, want, why in link_cases if usable_link(c, ip, rssi) != want]
check('connectivity table (pinned mirror): %d cases' % len(link_cases), not bad, str(bad))
check('the page reports the human status, including the setup hotspot',
      'doc["wifiStatus"] = _wifiManager.getStatusString();' in web and
      'if (_apMode) return "Setup Hotspot";' in wifi)
check('the status payload carries the driver state and signal',
      'doc["wifiState"] = _wifiManager.getRawStatusString();' in web and
      'doc["rssi"] = _wifiManager.getRssi();' in web,
      'the fields must carry the manager values, not just exist')
check('the page shows the driver state next to the status',
      'id="wifiState2"' in web and "data.wifiState" in web)
check('a failed poll is visibly marked instead of freezing the last values',
      'setLinkWarning(true);' in web and
      'id="connWarn"' in web and '.topbar-dot.stale' in read('src/DashboardAssets.h'))
check('the failed-poll handler covers the request, not the rendering',
      re.search(r'\}, function\(\) \{\s*\n\s*setLinkWarning\(true\);', web) is not None and
      '.catch(function() {' not in web,
      'a rendering bug must not be reported as a network outage')

print('\n%s (%d checks failed)' % ('ALL CHECKS PASSED' if not fails else 'FAILURES: ' + ', '.join(fails),
                                   len(fails)))
sys.exit(1 if fails else 0)
