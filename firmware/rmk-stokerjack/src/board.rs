use crate::{
    animation::{self, Status},
    telemetry::{self, STOKERJACK_TELEMETRY as T},
};
use core::sync::atomic::Ordering::Relaxed;
use embassy_futures::select::{Either, select};
use embassy_nrf::{pac, saadc::Saadc};
use embassy_time::{Duration, Instant, Timer};
use rmk::types::{
    action::Action,
    battery::{BatteryStatus, ChargeState},
    connection::ConnectionType,
};
use rmk::{core_traits::Runnable, event::*};
pub fn safe_led_off() {
    pac::P1.outset().write(|w| w.0 = 1 << 9);
    pac::P1.dirset().write(|w| w.0 = 1 << 9);
    release();
}
use crate::ost::{Ost, release};
pub struct Board {
    adc: Saadc<'static, 1>,
    ost: Ost,
}
impl Board {
    pub fn new(adc: Saadc<'static, 1>) -> Self {
        Self {
            adc,
            ost: Ost::new(),
        }
    }
}
impl Runnable for Board {
    async fn run(&mut self) -> ! {
        let mut keys = KeyboardEvent::subscriber();
        let mut actions = ActionEvent::subscriber();
        let mut layers = LayerChangeEvent::subscriber();
        let mut indicators = LedIndicatorEvent::subscriber();
        let mut connection = ConnectionStatusChangeEvent::subscriber();
        let mut sleep = SleepStateEvent::subscriber();
        let mut status = Status::default();
        let mut start = None;
        let mut powered = false;
        let mut sleeping = false;
        let mut last_show: Option<Instant> = None;
        let mut last_frame = u32::MAX;
        let mut next_frame = Instant::now();
        let mut adc_at = Instant::now();
        let mut tick_at = Instant::now();
        telemetry::stage(3);
        loop {
            let deadline = if powered {
                next_frame.min(tick_at)
            } else {
                tick_at.min(adc_at)
            };
            let event = select(
                select(
                    select(keys.next_event(), actions.next_event()),
                    select(layers.next_event(), indicators.next_event()),
                ),
                select(
                    select(connection.next_event(), sleep.next_event()),
                    Timer::at(deadline),
                ),
            )
            .await;
            let mut trigger = false;
            match event {
                Either::First(Either::First(Either::First(e))) => {
                    T.key_events.fetch_add(1, Relaxed);
                    if let KeyboardEventPos::Key(k) = e.pos {
                        let index = k.row as u32 * 10 + k.col as u32;
                        let cell = if index < 32 { &T.keys_lo } else { &T.keys_hi };
                        let mask = 1 << (index % 32);
                        if e.pressed {
                            cell.fetch_or(mask, Relaxed);
                        } else {
                            cell.fetch_and(!mask, Relaxed);
                        }
                    }
                    if e.pressed && last_show.is_none_or(|t| t.elapsed() >= Duration::from_secs(30))
                    {
                        trigger = true;
                    }
                }
                Either::First(Either::First(Either::Second(e))) => {
                    if e.action == Action::User(16) && e.keyboard_event.pressed {
                        trigger = true;
                    }
                }
                Either::First(Either::Second(Either::First(e))) => {
                    status.layer = e.0;
                    trigger = true;
                }
                Either::First(Either::Second(Either::Second(e))) => {
                    status.caps = e.0.caps_lock();
                    trigger = true;
                }
                Either::Second(Either::First(Either::First(e))) => {
                    status.profile = e.0.ble.profile;
                    status.transport = match e.0.decide_active() {
                        Some(ConnectionType::Usb) => 1,
                        Some(ConnectionType::Ble) => 2,
                        _ => 0,
                    };
                    T.usb.store(
                        matches!(e.0.usb, rmk::types::connection::UsbState::Configured) as u32,
                        Relaxed,
                    );
                    T.ble.store(
                        matches!(e.0.ble.state, rmk::types::ble::BleState::Connected) as u32,
                        Relaxed,
                    );
                    trigger = true;
                }
                Either::Second(Either::First(Either::Second(e))) => {
                    sleeping = e.0;
                    if sleeping {
                        start = None;
                    } else {
                        trigger = true;
                    }
                }
                _ => {}
            }
            if Instant::now() >= tick_at {
                telemetry::tick();
                tick_at = Instant::now() + Duration::from_millis(250);
            }
            if Instant::now() >= adc_at {
                let mut raw = [0i16];
                self.adc.sample(&mut raw).await;
                let mv = animation::battery_mv(raw[0]);
                status.battery = animation::nimh_percent(mv);
                T.battery_mv.store(mv, Relaxed);
                T.battery_pct
                    .store(status.battery.unwrap_or(255) as u32, Relaxed);
                publish_event(BatteryStatusEvent(match status.battery {
                    Some(level) => BatteryStatus::Available {
                        charge_state: ChargeState::Discharging,
                        level: Some(level),
                    },
                    None => BatteryStatus::Unavailable,
                }));
                adc_at = Instant::now() + Duration::from_secs(12);
            }
            if trigger && !sleeping {
                start = Some(Instant::now());
                last_show = start;
                last_frame = u32::MAX;
                next_frame = Instant::now();
            }
            if let Some(begin) = start {
                let frame = (begin.elapsed().as_millis() / 20) as u32;
                if frame >= animation::FRAMES {
                    start = None;
                } else if frame != last_frame {
                    last_frame = frame;
                    next_frame = begin + Duration::from_millis((frame as u64 + 1) * 20);
                    if !powered {
                        release();
                        pac::P1.outclr().write(|w| w.0 = 1 << 9);
                        Timer::after_millis(10).await;
                        powered = true;
                        T.led_power.store(1, Relaxed);
                    }
                    if !self.ost.write(animation::render(status, frame)).await {
                        start = None;
                    }
                }
            }
            if start.is_none() && powered {
                self.ost.write([animation::Rgb::default(); 4]).await;
                safe_led_off();
                powered = false;
                T.led_power.store(0, Relaxed);
            }
        }
    }
}
