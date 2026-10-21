//! Send only inside a radio-scheduled MPSL slot. No 0.6ms global IRQ lock.
use crate::animation::{Rgb, encode};
use core::{
    cell::UnsafeCell,
    sync::atomic::{
        AtomicU32,
        Ordering::{Acquire, Relaxed, Release},
    },
};
use embassy_nrf::pac;
use embassy_time::Timer;
use nrf_mpsl::raw;
static DATA: [AtomicU32; 3] = [const { AtomicU32::new(0) }; 3];
static RESULT: AtomicU32 = AtomicU32::new(0);
struct ReturnParam(UnsafeCell<raw::mpsl_timeslot_signal_return_param_t>);
// Only MPSL's serialized callback writes this return structure.
unsafe impl Sync for ReturnParam {}
static RETURN: ReturnParam =
    ReturnParam(UnsafeCell::new(raw::mpsl_timeslot_signal_return_param_t {
        callback_action: raw::MPSL_TIMESLOT_SIGNAL_ACTION_END as u8,
        params: raw::mpsl_timeslot_signal_return_param_t__bindgen_ty_1 {
            request: raw::mpsl_timeslot_signal_return_param_t__bindgen_ty_1__bindgen_ty_1 {
                p_next: core::ptr::null_mut(),
            },
        },
    }));
pub fn release() {
    pac::P0.pin_cnf(27).write(|w| w.0 = 2);
}
unsafe extern "C" fn callback(_: u8, signal: u32) -> *mut raw::mpsl_timeslot_signal_return_param_t {
    match signal {
        raw::MPSL_TIMESLOT_SIGNAL_START => {
            for word in &DATA {
                for byte in word.load(Acquire).to_le_bytes() {
                    for bit in (0..8).rev() {
                        if byte & (1 << bit) != 0 {
                            pac::P0.outset().write(|w| w.0 = 1 << 27);
                        } else {
                            pac::P0.outclr().write(|w| w.0 = 1 << 27);
                        }
                        pac::P0.dirset().write(|w| w.0 = 1 << 27);
                        cortex_m::asm::delay(192);
                        release();
                        cortex_m::asm::delay(192);
                    }
                }
            }
            RESULT.store(1, Release);
            RETURN.0.get()
        }
        raw::MPSL_TIMESLOT_SIGNAL_BLOCKED | raw::MPSL_TIMESLOT_SIGNAL_CANCELLED => {
            RESULT.store(2, Release);
            core::ptr::null_mut()
        }
        raw::MPSL_TIMESLOT_SIGNAL_OVERSTAYED => {
            release();
            RESULT.store(3, Release);
            RETURN.0.get()
        }
        _ => core::ptr::null_mut(),
    }
}
pub struct Ost {
    session: u8,
}
impl Ost {
    pub fn new() -> Self {
        let mut session = 0;
        let result = unsafe { raw::mpsl_timeslot_session_open(Some(callback), &mut session) };
        assert_eq!(result, 0);
        Self { session }
    }
    pub async fn write(&mut self, p: [Rgb; 4]) -> bool {
        let bytes = encode(p);
        for i in 0..3 {
            DATA[i].store(
                u32::from_le_bytes(bytes[i * 4..i * 4 + 4].try_into().unwrap()),
                Release,
            );
        }
        RESULT.store(0, Release);
        let request = raw::mpsl_timeslot_request_t {
            request_type: raw::MPSL_TIMESLOT_REQ_TYPE_EARLIEST as u8,
            params: raw::mpsl_timeslot_request_t__bindgen_ty_1 {
                earliest: raw::mpsl_timeslot_request_earliest_t {
                    hfclk: raw::MPSL_TIMESLOT_HFCLK_CFG_XTAL_GUARANTEED as u8,
                    priority: raw::MPSL_TIMESLOT_PRIORITY_NORMAL as u8,
                    length_us: 3000,
                    timeout_us: 100000,
                },
            },
        };
        let result = unsafe { raw::mpsl_timeslot_request(self.session, &request) };
        if result != 0 {
            return false;
        }
        while RESULT.load(Acquire) == 0 {
            Timer::after_millis(1).await;
        }
        Timer::after_micros(3500).await;
        let ok = RESULT.load(Acquire) == 1;
        if ok {
            crate::telemetry::STOKERJACK_TELEMETRY
                .led_frames
                .fetch_add(1, Relaxed);
        }
        ok
    }
}
