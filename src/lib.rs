extern "C" {
    pub fn mco_coro_init(f: *const u8, user_data: *const u8) -> *const u8;
    pub fn mco_coro_get_user_data(k: *const u8) -> *const u8;
    pub fn mco_coro_free(k: *const u8) -> u8;
    pub fn mco_coro_is_suspended(k: *const u8) -> bool;
    pub fn mco_coro_push(k: *const u8, arg: *const u8, size: usize) -> u8;
    pub fn mco_coro_pop(k: *const u8, arg: *mut u8, size: usize) -> u8;
    pub fn mco_coro_suspend(k: *const u8) -> u8;
    pub fn mco_coro_resume(k: *const u8) -> u8;
    pub fn mco_coro_running() -> *const u8;
    pub fn mco_coro_bytes_stored(k: *const u8) -> usize;
    pub fn mco_coro_transfer(src: *const u8, dst: *const u8, len: usize) -> u8;

    pub fn mco_abort_call(buf: *mut u8, body: unsafe extern "C" fn(*mut u8), env: *mut u8) -> i32;
    pub fn mco_abort_longjmp(buf: *mut u8, val: i32) -> !;
}
