extern "C" {
    pub fn mco_coro_init(f: *const u8) -> *const u8;
    pub fn mco_coro_free(k: *const u8) -> u8;
    pub fn mco_coro_is_suspended(k: *const u8) -> bool;
    pub fn mco_coro_push(k: *const u8, arg: *const u8, size: usize) -> u8;
    pub fn mco_coro_pop(k: *const u8, arg: *mut u8, size: usize) -> u8;
    pub fn mco_coro_suspend(k: *const u8) -> u8;
    pub fn mco_coro_resume(k: *const u8) -> u8;
}
