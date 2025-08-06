# BLE Charakteristiken Referenz

Diese Datei dokumentiert alle BLE (Bluetooth Low Energy) Charakteristiken des E-Bike Controllers für die Entwicklung von mobilen Apps.

## Device Information Service (180A)

Standard Bluetooth Service für Geräteinformationen.

| Characteristic | UUID | Type | Beschreibung |
|----------------|------|------|-------------|
| Manufacturer Name | 2A29 | Read | "OpenSource E-Bike" |
| Model Number | 2A24 | Read | "ESP32-Controller-v1.0" |
| Firmware Revision | 2A26 | Read | "1.0.0" |

## Telemetry Service (12345678-1234-1234-1234-123456789abc)

Hauptservice für alle E-Bike Telemetriedaten.

| Characteristic | UUID | Type | Format | Beschreibung |
|----------------|------|------|--------|-------------|
| Speed | ...a001 | Read/Notify | Float (4 bytes) | Geschwindigkeit in km/h |
| Cadence | ...a002 | Read/Notify | Float (4 bytes) | Trittfrequenz in RPM |
| Torque | ...a003 | Read/Notify | Float (4 bytes) | Drehmomnet in Nm |
| Battery | ...a004 | Read/Notify | UInt8 (1 byte) | Batteriestand in % |
| Motor Current | ...a005 | Read/Notify | Float (4 bytes) | Motorstrom in A |
| VESC Data | ...a006 | Read/Notify | JSON String | Erweiterte VESC-Daten |
| System Status | ...a007 | Read/Notify | JSON String | Systemstatus und Mode |

### VESC Data JSON Format
```json
{
  "motor_rpm": 1250.5,
  "duty_cycle": 45.2,
  "temp_mosfet": 38.5,
  "temp_motor": 42.1,
  "battery_voltage": 48.2,
  "amp_hours": 2.45,
  "watt_hours": 118.5
}
```

### System Status JSON Format
```json
{
  "mode": 2,
  "mode_name": "Urban",
  "motor_enabled": true,
  "timestamp": 123456789
}
```

## Control Service (12345678-1234-1234-1234-123456789def)

Service für Steuerung und Kontrolle des E-Bikes.

| Characteristic | UUID | Type | Format | Beschreibung |
|----------------|------|------|--------|-------------|
| Mode Control | ...b001 | Write | UInt8 (1 byte) | Mode-Nummer zum Wechseln |
| Mode List | ...b002 | Read/Notify | JSON String | Verfügbare Modi |
| Command | ...b003 | Write | String | Text-Kommandos |

### Mode List JSON Format
```json
{
  "modes": [
    {
      "index": 0,
      "name": "Linear",
      "description": "linear profile",
      "hasLight": true
    },
    {
      "index": 1,
      "name": "Touring",
      "description": "Fast start-up, gentle slope to 30km/h",
      "hasLight": false
    }
  ]
}
```

### Verfügbare Kommandos
- `GET_STATUS` - Aktuelle Status-Updates anfordern
- `GET_MODES` - Mode-Liste anfordern
- `EMERGENCY_STOP` - Notfall-Stop (wechselt zu "No Assist" Mode)

## Verbindungsbeispiel (Android/Kotlin)

```kotlin
// Service UUIDs
val TELEMETRY_SERVICE_UUID = UUID.fromString("12345678-1234-1234-1234-123456789abc")
val CONTROL_SERVICE_UUID = UUID.fromString("12345678-1234-1234-1234-123456789def")

// Charakteristik UUIDs  
val SPEED_CHAR_UUID = UUID.fromString("12345678-1234-1234-1234-12345678a001")
val MODE_CONTROL_CHAR_UUID = UUID.fromString("12345678-1234-1234-1234-12345678b001")

// Speed Notification abonnieren
val speedCharacteristic = gatt.getService(TELEMETRY_SERVICE_UUID)
    .getCharacteristic(SPEED_CHAR_UUID)
gatt.setCharacteristicNotification(speedCharacteristic, true)

// Mode wechseln (Beispiel: Mode 2)
val modeCharacteristic = gatt.getService(CONTROL_SERVICE_UUID)
    .getCharacteristic(MODE_CONTROL_CHAR_UUID)
modeCharacteristic.value = byteArrayOf(2)
gatt.writeCharacteristic(modeCharacteristic)
```

## Verbindungsbeispiel (iOS/Swift)

```swift
// Service UUIDs
let telemetryServiceUUID = CBUUID(string: "12345678-1234-1234-1234-123456789abc")
let controlServiceUUID = CBUUID(string: "12345678-1234-1234-1234-123456789def")

// Charakteristik UUIDs
let speedCharUUID = CBUUID(string: "12345678-1234-1234-1234-12345678a001")
let modeControlCharUUID = CBUUID(string: "12345678-1234-1234-1234-12345678b001")

// Speed Notification abonnieren
if let speedChar = peripheral.services?.first(where: { $0.uuid == telemetryServiceUUID })?
    .characteristics?.first(where: { $0.uuid == speedCharUUID }) {
    peripheral.setNotifyValue(true, for: speedChar)
}

// Mode wechseln (Beispiel: Mode 2)
if let modeChar = peripheral.services?.first(where: { $0.uuid == controlServiceUUID })?
    .characteristics?.first(where: { $0.uuid == modeControlCharUUID }) {
    let modeData = Data([2])
    peripheral.writeValue(modeData, for: modeChar, type: .withResponse)
}
```

## Update-Frequenzen

- **Telemetrie-Daten**: 2 Sekunden (0.5 Hz)
- **VESC-Daten**: 2 Sekunden (0.5 Hz)
- **System Status**: Bei Änderungen oder auf Anfrage
- **Mode List**: Bei Änderungen oder auf Anfrage

## Fehlerbehandlung

- Bei Verbindungsabbruch startet der ESP32 automatisch wieder Advertising
- Ungültige Mode-Nummern werden ignoriert
- Unbekannte Kommandos werden in den Logs vermerkt
- Timeout bei Semaphore-Zugriff führt zu Fehlermeldung statt Systemabsturz

## Entwicklungshinweise

1. **Notifications abonnieren**: Für Live-Daten immer Notifications aktivieren
2. **Thread-Safety**: Alle BLE-Operationen sind thread-safe implementiert
3. **Energieeffizienz**: 2s Update-Rate optimiert für Akkulaufzeit
4. **Reconnection**: Apps sollten automatisches Reconnection implementieren
5. **JSON Parsing**: Robuste JSON-Parser für VESC/Status-Daten verwenden
