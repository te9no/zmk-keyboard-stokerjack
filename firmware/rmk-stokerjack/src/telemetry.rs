use core::sync::atomic::{AtomicU32, Ordering};
#[used]
#[unsafe(no_mangle)]
pub static STOKERJACK_ROM_ID: [u8; 26] = *b"STOKERJACK-RMK-OST60-v001\0";
#[repr(C)]
pub struct Telemetry {
    pub magic: AtomicU32,
    pub version: AtomicU32,
    pub build: AtomicU32,
    pub stage: AtomicU32,
    pub heartbeat: AtomicU32,
    pub battery_mv: AtomicU32,
    pub battery_pct: AtomicU32,
    pub key_events: AtomicU32,
    pub keys_lo: AtomicU32,
    pub keys_hi: AtomicU32,
    pub usb: AtomicU32,
    pub ble: AtomicU32,
    pub led_frames: AtomicU32,
    pub led_power: AtomicU32,
    pub challenge: AtomicU32,
    pub response: AtomicU32,
}
#[used]
#[unsafe(no_mangle)]
pub static STOKERJACK_TELEMETRY: Telemetry = Telemetry {
    magic: AtomicU32::new(0x534A524D),
    version: AtomicU32::new(1),
    build: AtomicU32::new(0x20261017),
    stage: AtomicU32::new(0),
    heartbeat: AtomicU32::new(0),
    battery_mv: AtomicU32::new(0),
    battery_pct: AtomicU32::new(255),
    key_events: AtomicU32::new(0),
    keys_lo: AtomicU32::new(0),
    keys_hi: AtomicU32::new(0),
    usb: AtomicU32::new(0),
    ble: AtomicU32::new(0),
    led_frames: AtomicU32::new(0),
    led_power: AtomicU32::new(0),
    challenge: AtomicU32::new(0),
    response: AtomicU32::new(0),
};
pub fn stage(v: u32) {
    STOKERJACK_TELEMETRY.stage.store(v, Ordering::Relaxed)
}
pub fn tick() {
    let t = &STOKERJACK_TELEMETRY;
    t.heartbeat.fetch_add(1, Ordering::Relaxed);
    t.response.store(
        t.challenge.load(Ordering::Relaxed) ^ 0xA537C9E1,
        Ordering::Relaxed,
    );
}
