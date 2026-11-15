# DeyeBridge mit Home Assistant verbinden

DeyeBridge publishes Werte und Entity-Definitionen per MQTT. Sobald die
MQTT-Integration in Home Assistant aktiv ist und der Broker im Web-UI
eingetragen wurde, erscheinen alle Entities automatisch (MQTT Discovery).

## Topics

| Zweck | Topic |
|-------|-------|
| Sensorwerte | `<base>/sensor/<key>` (z. B. `deye/sensor/battery_soc`) |
| JSON-Gesamtstatus | `<base>/state` |
| Verfügbarkeit (LWT) | `<base>/status` (`online`/`offline`, retained) |
| Steuerung | `<base>/set/<key>` (z. B. `deye/set/max_sell_power`) |
| Victron BLE Solar Charger | `<base>/victron/<mac>` (JSON, MAC ohne Trennzeichen) |
| HA-Discovery | `homeassistant/<typ>/deye-bridge/<key>/config` (retained) |
| Victron HA-Discovery | `homeassistant/sensor/deye-bridge-victron/<mac>_<key>/config` (retained) |

## Victron Solar Charger per BLE

Optional kann ein Victron-Solar-Laderegler über BLE eingebunden werden. Dazu
MAC-Adresse und 32-stelligen BLE-Schlüssel im Web-Setup eintragen und speichern.
Nach dem Neustart veröffentlicht DeyeBridge die Messwerte als JSON unter
`<base>/victron/<mac>` und legt ein eigenes Victron-Gerät mit diesen HA-Sensoren
an: Batteriespannung, Batteriestrom, PV-Leistung, Tagesertrag, kumulierter
Gesamtenergiezähler und Ladestatus.
Für die Diagnose ohne seriellen Zugriff zeigt `/diag` `victron_configured`
(Konfiguration gültig) und `victron_data_received` (BLE-Daten empfangen) an.
Zusätzlich wird `yield_total` als kumulierter Energiezähler (`kWh`,
`total_increasing`) veröffentlicht und in NVS gespeichert. Da Victron BLE nur
den Tagesertrag liefert, startet der Zähler bei der ersten Aktivierung mit dem
aktuellen Tagesertrag; ein historischer Lebenszeitwert ist nicht verfügbar.

Der eigene Topic-Zweig und das Discovery-Präfix `deye-bridge-victron` halten
die Victron-Entities getrennt von den Deye-Inverter-Entities. Ohne vollständige
MAC-Adresse und Schlüssel bleibt die BLE-Erfassung deaktiviert.

## Sensor-Entities

Vollständige Abbildung des Register-Satzes der Referenz-ESPHome-Konfiguration:

- PV: `pv1_power` … `pv4_power`, `pv_total_power`, `pv1_voltage` … `pv4_voltage`, `pv1_current` … `pv4_current`
- Batterie: `battery_soc`, `battery_voltage`, `battery_power`, `battery_current`, `battery_temperature`, `battery_capacity_shutdown`
- Netz: `grid_power`, `grid_l1_power` … `grid_l3_power`, `grid_voltage_l1` … `grid_voltage_l3`,
  `grid_current_l1` … `grid_current_l3`, `grid_frequency`, `ct_power`
- Wechselrichter: `inverter_power`, `inverter_l1_power` … `inverter_l3_power`,
  `inverter_l1_voltage` … `inverter_l3_voltage`, `inverter_frequency`
- Last/Gen: `load_power`, `load_l1_power` … `load_l3_power`, `gen_power`, `gen_l1_power` … `gen_l3_power`
- Energiezähler: `day_*` und `total_*` (kWh, `total_increasing`)
- Temperaturen: `dc_transformer_temperature`, `radiator_temperature`
- Status: `overall_state` (Text), `grid_connected` (binary_sensor), `system_mode_time1` … `system_mode_time6`,
  `time_slot_1` … `time_slot_6` (Text „HH:MM")

## Steuer-Entities

| Entity | Register | Typ | Bereich |
|--------|----------|-----|---------|
| Zero Export | 104 | number | -20–20 W |
| Battery Max Charge Current | 108 | number | 0–210 A, Schritt 5 |
| Battery Max Discharge Current | 109 | number | 0–210 A, Schritt 5 |
| Grid Charge Start SOC | 127 | number | 0–100 % |
| Grid Charge Current | 128 | number | 0–210 A, Schritt 5 |
| Max Sell Power | 143 | number | 0–12000 W, Schritt 100 |
| System Mode Power Time 1–6 | 154–159 | number | 0–12000 W, Schritt 100 |
| System Mode V Time 1–6 | 160–165 | number | 4320–5760 (V×10), Schritt 10 |
| System Mode SOC Time 1–6 | 166–171 | number | 0–100 % |
| Inverter ON/OFF | 80 | switch | ON/OFF (Bit 0) |
| Solar Sell | 145 | switch | ON/OFF (Bit 0) |
| Time of Use | 146 | switch | ON = 0x00FF (alle Tage), OFF = 0 |
| Grid Charge Time 1–6 | 172–177 | switch | ON/OFF (Bit 0) |
| Energy Pattern | 141 | select | Battery first / Load first |
| Work Mode | 142 | select | Selling First / Zero Export + Limit to Load Only / Limited to Home |

Bitmask-Register (80, 145, 172–177) werden per **Read-Modify-Write** über den
Cache beschrieben – andere Bits im Register bleiben erhalten, genau wie die
ESPHome-Switches mit `bitmask: 1`.

Beispiel Steuerung per MQTT:

```text
deye/set/max_sell_power  ->  "5000"
deye/set/solar_sell      ->  "ON"
```

## Gleichzeitige Steuerung mit evcc

Beide Systeme schreiben in dieselbe Write-Queue – **letzter Befehl gewinnt**.
Damit sich evcc und HA nicht gegenseitig stören:

- evcc überschreibt im Batteriemodus laufend die Register 127, 146 und 166–171.
  HA-Steuer-Entities für diese Register sinnvoll **nur lesend** nutzen oder
  nur dann schreiben, wenn evcc den Batteriemodus gerade nicht aktiv steuert.
- Konfliktfrei parallel: Register 143 (Max Sell Power), 145 (Solar Sell) und
  154–165 (Time-Slot-Parameter) werden von evcc nicht angefasst.

## batteriespezifische Automatisierung

Für Batteriesteuerung komplett über HA (ohne evcc-Batterie-Treiber) genügt es,
die Number-/Switch-Entities in Automatisierungen zu setzen – z. B. Netzladung:

1. `deye/set/grid_charge_current` = 60
2. `deye/set/grid_charge_start_soc` = 80
3. `deye/set/time_of_use` = OFF  (Register 130 im Deye steuert das Grid-Charging
   selbst; Register 146/TOU-Steuerung je nach gewünschtem Verhalten)
