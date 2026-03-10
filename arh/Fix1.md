**План**
1. Закомітити й перевірити поточний `mqtt_bridge` stack-fault fix окремо.
   Поки він не закомічений, оцінка архітектури не збігається з реальним deployable станом. Спершу треба зафіксувати [mqtt_bridge.hpp](/home/alex/dev/esp/zigbee-gateway_v1/components/mqtt_bridge/include/mqtt_bridge.hpp), [mqtt_bridge.cpp](/home/alex/dev/esp/zigbee-gateway_v1/components/mqtt_bridge/mqtt_bridge.cpp), [mqtt_device_sync.cpp](/home/alex/dev/esp/zigbee-gateway_v1/components/mqtt_bridge/mqtt_device_sync.cpp), [service_runtime_api.hpp](/home/alex/dev/esp/zigbee-gateway_v1/components/service/include/service_runtime_api.hpp), [service_runtime.hpp](/home/alex/dev/esp/zigbee-gateway_v1/components/service/include/service_runtime.hpp), [service_runtime.cpp](/home/alex/dev/esp/zigbee-gateway_v1/components/service/service_runtime.cpp), а потім перепрошити й перевірити boot path на залізі.

2. Прибрати з `ServiceRuntimeApi` сирий `CoreState` для bridge consumers.
   Наступний крок: прибрати або депрекейтнути [service_runtime_api.hpp](/home/alex/dev/esp/zigbee-gateway_v1/components/service/include/service_runtime_api.hpp#L116) і [service_runtime_api.hpp](/home/alex/dev/esp/zigbee-gateway_v1/components/service/include/service_runtime_api.hpp#L117) як bridge-facing контракт. Для мостів потрібен не `CoreState`, а вузький service-owned snapshot/read model.

3. Ввести окремий `MqttBridgeSnapshot` у service layer.
   У service треба додати агрегований snapshot builder, який already normalized і transport-ready:
   - availability
   - power state
   - telemetry payload fields
   - revision/identity needed for diff
   Тоді [mqtt_device_sync.cpp](/home/alex/dev/esp/zigbee-gateway_v1/components/mqtt_bridge/mqtt_device_sync.cpp) перестане знати про `CoreDeviceRecord` і буде лише серіалізувати свій bridge DTO.

4. Після цього перевести MQTT bridge повністю на service-owned snapshot API.
   У [mqtt_bridge.hpp](/home/alex/dev/esp/zigbee-gateway_v1/components/mqtt_bridge/include/mqtt_bridge.hpp) і [mqtt_bridge.cpp](/home/alex/dev/esp/zigbee-gateway_v1/components/mqtt_bridge/mqtt_bridge.cpp) прибрати залежність від `core_state.hpp` як основного transport contract. MQTT має залежати від `service_runtime_api.hpp` + `MqttBridgeSnapshot`, а не від core layout.

5. Повторити той самий патерн для Matter bridge.
   У [matter_bridge.cpp](/home/alex/dev/esp/zigbee-gateway_v1/components/matter_bridge/matter_bridge.cpp#L108) зараз той самий drift: bridge сам читає `CoreState`, сам infer-ить class і сам робить diff. Потрібен service-owned `MatterBridgeSnapshot` / `MatterBridgeDelta`, щоб Matter не залежав від `CoreDeviceRecord` напряму.

6. Довести Matter до реального production wiring.
   Після появи вузького snapshot API:
   - `attach_runtime`
   - runtime feed/drain loop
   - 1 host regression test
   - 1 інваріант у [check_arch_invariants.sh](/home/alex/dev/esp/zigbee-gateway_v1/check_arch_invariants.sh)
   Це закриє проблему “extension module є, але production path не wired”.

7. Звузити concrete `ServiceRuntime` ще на один рівень.
   Не робити великий rewrite. Практичний крок:
   - виділити окремий internal coordinator для bridge snapshots/read models
   - забрати bridge-specific concerns із загального orchestrator surface
   - зменшити кількість причин, через які bridge changes торкаються [service_runtime.hpp](/home/alex/dev/esp/zigbee-gateway_v1/components/service/include/service_runtime.hpp)

8. Винести HAL Zigbee test hook з public header.
   [hal_zigbee.h](/home/alex/dev/esp/zigbee-gateway_v1/components/app_hal/include/hal_zigbee.h#L153) треба очистити від `SERVICE_RUNTIME_TEST_HOOKS` surface:
   - або окремий `hal_zigbee_test.h`
   - або test-only adapter у `test/`
   Це дрібніше за попередні кроки, але добре закриває чистоту HAL boundary.

9. Після кожного кроку зашивати регресію автоматично.
   Мінімум після кожного архітектурного кроку:
   - [scripts/run_blocking_local_checks.sh](/home/alex/dev/esp/zigbee-gateway_v1/scripts/run_blocking_local_checks.sh)
   - якщо чіпав bridge runtime path, ще й відповідний HIL/boot smoke на залізі
   - новий інваріант у [check_arch_invariants.sh](/home/alex/dev/esp/zigbee-gateway_v1/check_arch_invariants.sh), якщо це recurring drift

**Оптимальна послідовність**
1. Закомітити `mqtt_bridge` crash fix і перевірити на hardware  
2. `MqttBridgeSnapshot` у service  
3. Перевести MQTT на snapshot API  
4. `MatterBridgeSnapshot` у service  
5. Довести Matter production wiring  
6. Звузити `ServiceRuntime` bridge/read-model responsibilities  
7. Винести HAL Zigbee test hook з public header

Якщо хочеш, я можу одразу почати з пунктів 1-3 в одному заході.

**План Закриття Phase 2**
Поточний стан: виконано і закрито. MQTT bridge працює як реальна мережева інтеграція з broker, host regressions зелені, HIL smoke з реальним broker пройшов.

**Критерії Закриття**
1. Gateway встановлює реальне MQTT-з'єднання з broker, а не лише формує локальні publish records.
2. Device snapshots реально публікуються в broker у стабільному topic contract, описаному в [README.md](/home/alex/dev/esp/zigbee-gateway_v1/README.md).
3. MQTT command ingress реально приймає команди з broker і проводить їх через service runtime.
4. Є reconnect/error policy для broker connection, яка не ламає основний gateway lifecycle.
5. Є automated tests для serializer/topic contract/runtime feed і хоча б один HIL smoke з реальним broker.

**Конкретні Кроки**
1. Додати реальний MQTT transport adapter поверх ESP-IDF MQTT client.
   Мінімальний scope:
   - broker URI / auth / client id config
   - connect / disconnect / reconnect callbacks
   - publish API для `MqttPublishedMessage`
   - subscribe API для command topics
   Правильне місце: новий thin adapter у `components/app_hal` або окремий transport module, без доменної логіки в transport layer.

2. Підключити MQTT bridge до реального transport path.
   У [components/mqtt_bridge/mqtt_bridge.cpp](/home/alex/dev/esp/zigbee-gateway_v1/components/mqtt_bridge/mqtt_bridge.cpp) замінити log-only `publish_message()` на виклик transport adapter. Локальний batching/drain лишити в bridge, transport лише доставляє publish/subscribe.

3. Додати реальний command ingress з broker у service runtime.
   Потрібно визначити й зафіксувати command topics:
   - power on/off
   - optional reconfigure/reporting commands, якщо вони вже підтримані service layer
   Bridge має:
   - приймати payload з transport
   - парсити в narrow DTO
   - викликати existing runtime API
   - повертати status/result у вже наявний async operation model або окремий MQTT result topic

4. Ввести MQTT runtime/config status у service/read model.
   Мінімум:
   - enabled/disabled
   - connected/disconnected
   - last connect error
   - broker endpoint summary
   Це потрібно і для web observability, і для HIL smoke, і для безпечної діагностики Phase 2.

5. Закріпити topic contract і config contract в коді та документації.
   Після real transport integration оновити:
   - [README.md](/home/alex/dev/esp/zigbee-gateway_v1/README.md)
   - за потреби [ARCHITECTURE.md](/home/alex/dev/esp/zigbee-gateway_v1/ARCHITECTURE.md)
   У документації має бути не “prepared”, а конкретний production contract:
   - topics
   - retained/non-retained behavior
   - QoS policy
   - command payload shapes
   - error behavior

6. Додати automated regression tests саме для real Phase 2 behavior.
   Мінімальний набір:
   - host test на MQTT transport-independent publish path з fake transport
   - host test на subscribe command -> runtime ingress
   - test на reconnect policy/state transitions
   - existing runtime feed tests лишити як low-level regression layer

7. Додати HIL smoke для MQTT Phase 2.
   Окремий сценарій у `scripts/` або `test/hil/`:
   - gateway boot
   - broker connect
   - join device
   - verify device publish in broker
   - publish `ON`
   - verify command executed
   - remove device
   - verify offline/remove publish
   Це і буде фактичним доказом закриття Phase 2.

8. Додати архітектурні інваріанти під MQTT production path.
   Мінімум два:
   - bridge не має повертатись до raw `CoreState`
   - transport adapter не має містити доменної логіки device mapping / serializer policy
   Якщо з'явиться окремий MQTT transport module, це треба зашити в [check_arch_invariants.sh](/home/alex/dev/esp/zigbee-gateway_v1/check_arch_invariants.sh).

9. Формально закрити Phase 2 тільки після виконання Definition of Done.
   Definition of Done:
   - `scripts/run_blocking_local_checks.sh` passed
   - target build passed
   - HIL smoke з реальним broker passed
   - `README.md` більше не каже `Phase 2 | Module structure prepared`, а відображає завершений Phase 2 state

**Фактичний Статус Закриття**
- `scripts/run_blocking_local_checks.sh` passed
- target build passed
- `scripts/run_gateway_mqtt_hil_smoke.sh` passed з реальним broker
- `README.md` оновлено до завершеного Phase 2 state
- MQTT broker path включає:
  - real transport adapter
  - real publish path
  - real subscribe/command ingress
  - MQTT status/read model
  - Home Assistant MQTT discovery

**Рекомендована Послідовність**
1. MQTT transport adapter
2. Real publish path
3. Real subscribe/command ingress
4. MQTT status/read model
5. Tests
6. HIL smoke
7. Docs + formal phase close
