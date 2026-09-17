// csp_webots.c -- the Crazyflie with the hardware simulated.
//
// The other ports translate between a CandySpeak declaration and a peripheral
// register. This one translates between a declaration and Webots' controller
// API, and the interesting half is that it does it at the SAME LEVEL: the
// program reads a 14-byte MPU9250 burst whether the bytes come off I2C3 or out
// of wb_gyro_get_values, and it reads height in millimetres whether a VL53L1x
// measured them or a simulated rangefinder did.
//
// That is deliberate and it is what makes this worth building. private/pilot
// runs UNCHANGED -- same main.csp, same pins/, same scale factors -- so what
// the simulator exercises is the program that flies the drone, not a variant
// of it written for the simulator. The pin files are the interface, and this
// is the proof.
//
// It also reaches a part no stimulus file can. `-F` writes FIELDS (`Gx=0
// Mm=20`); a sensor writes BYTES, through buf_mark_fields and the commit, and
// the big-endian unpacking in imu.csp has never run on anything but a
// hand-written frame until now.
//
// THE CLOCK IS THE SIMULATION'S. wb_robot_step is what advances it, so time
// here is neither the wall clock nor a virtual counter the loop bumps -- it is
// how far the physics has been integrated. A cycle that takes too long does
// not drift the drone; it just runs slower than real time on the screen.

#include "csp.h"
#include "csp_print.h"

// DOES THIS BUILD HAVE A COMPILER? csp_rt_init wants its state, or NULL for a
// node that only runs images. This one normally HAS one -- the prompt on 2323
// is the reason to simulate rather than replay -- but the tier is the driver's
// decision, so it is spelled out here the way every other port spells it out.
#if defined(CSP_EXEC_ONLY)
#define CSP_CSTATE NULL
#else
#include "csp_compile.h"
#define CSP_CSTATE csp_cstate()
#endif
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#include <webots/robot.h>
#include <webots/motor.h>
#include <webots/gyro.h>
#include <webots/accelerometer.h>
#include <webots/distance_sensor.h>
#include <webots/gps.h>
#include <webots/keyboard.h>
#include <webots/device.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

// ------------------------------------------------------------------ devices

static WbDeviceTag dev_motor[4];
static WbDeviceTag dev_gyro, dev_acc, dev_tof, dev_gps;
static int         timestep;             // ms per wb_robot_step

// The four motors, in the order pilot names them: M1 front-right CCW, M2
// back-right CW, M3 back-left CCW, M4 front-left CW. The SIGN is what makes
// yaw work -- get one wrong and the drone flies, slowly spins, and cannot be
// told to stop (pins/motors.csp has the diagram).
static const double motor_dir[4] = { -1.0, +1.0, -1.0, +1.0 };

// Full scale, the two numbers pins/imu.csp derives ACC_SCALE and GYRO_SCALE
// from. Stated as the datasheet states them so the two files can be compared
// by eye: +-16 g at 2048 LSB/g, +-2000 dps at 16.4 LSB/dps.
#define ACC_LSB_PER_MS2   (2048.0 / 9.81)
#define GYRO_LSB_PER_RADS (16.4 * 57.29578)

// pins/link.csp's #define block. A #define is a COMPILE-TIME name in
// CandySpeak -- it folds into the instruction and never reaches the image --
// so there is nothing to read them out of and they are restated here. The
// three numbers are the wire protocol between this port and that program, and
// changing one without the other is a drone that ignores its radio.
#define CMD_GO    1
#define CMD_ABORT 2
#define CMD_LAND  3

// ------------------------------------------------------------------ TUNING
//
// THRUST TO ROTOR SPEED, and the one number in this file that is a guess.
//
// WHAT SETS IT: the FLOOR, not the ceiling. main.csp clips thrust to 0.05 at
// the bottom -- that is "as near off as this program goes" -- and whatever that
// maps to has to be well UNDER hover, or the drone climbs no matter what the
// altitude PID asks for. The controller then has no authority downwards and the
// drone leaves the world, which is exactly what 600 did:
//
// HOVER IS 55.4 rad/s, and that is arithmetic rather than opinion. The PROTO
// gives four propellers at thrustConstant 4e-05 and a mass of 0.05 kg:
//
//     4 * 4e-05 * w^2  =  0.05 * 9.81     ->   w = 55.4
//
// Confirmed against the model with --spin=56, which climbed at a steady
// 0.225 m/s^2 -- i.e. a force of 0.5018 N where the constants predict 0.5018.
// Earlier guesses of 70 and 80 came from watching WHEN the drone left the
// ground during a ramp, which measures the ramp and not the rotors: just above
// hover she accelerates at a fiftieth of g, so the first five centimetres take
// two seconds and the ramp runs on regardless.
//
//     MOTOR_MAX_RADS   thrust 0.05     hover needs u
//          600          134 rad/s         0.009      climbs at idle
//           80           18 rad/s         0.490      idles; hover mid-range
//
// 80 is 55.4 * sqrt(2): hover lands at u = 0.49, so the mixer has as much
// authority above hover as below it. That is what a real airframe is built
// with, and it is the only choice here that is a fact about the model rather
// than about one flight.
//
// The SHAPE is not a guess: thrust goes as the square of rotor speed, so the
// square root is what makes a doubling of commanded thrust feel like a doubling
// to the controller. Only the number is up for argument.
#define MOTOR_MAX_RADS 80.0

// ------------------------------------------------------------------ the console

// ------------------------------------------------------------------ the prompt
//
// A TERMINAL ON A SOCKET, because a Webots controller has no terminal. Its
// stdout goes to the simulator's log pane, which is one-way -- so a prompt
// there would print and never hear an answer.
//
//     telnet localhost 2323        (or: nc localhost 2323)
//
// Everything the node prints goes to whoever is connected, and falls back to
// the log when nobody is. That is what makes this a place to MECK rather than
// a place to watch: the interpreter is linked in (this port is not built
// exec-only, unlike a board that has no room for it), so a live drone can be
// asked `/state Alt`, told `#variable Foo = 1`, and have a rule added mid-air.
#ifndef CSP_WEBOTS_PORT
#define CSP_WEBOTS_PORT 2323
#endif

static int term_lfd = -1;                // listening
static int term_cfd = -1;                // the one client

static void term_listen(void)
{
    struct sockaddr_in a;
    int on = 1;

    if ((term_lfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	return;
    setsockopt(term_lfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // local only: it is a prompt
    a.sin_port = htons(CSP_WEBOTS_PORT);
    if ((bind(term_lfd, (struct sockaddr*)&a, sizeof(a)) < 0) ||
	(listen(term_lfd, 1) < 0)) {
	close(term_lfd);
	term_lfd = -1;
	return;
    }
    fcntl(term_lfd, F_SETFL, fcntl(term_lfd, F_GETFL, 0) | O_NONBLOCK);
}

// ONE CLIENT, and a new one replaces the old. Two prompts on one node is a
// question nobody asked, and dropping the previous connection is how a session
// left open by a crashed telnet is recovered without restarting the world.
static void term_poll(csp_rt_t* st)
{
    char c;

    if (term_lfd >= 0) {
	int fd = accept(term_lfd, NULL, NULL);

	if (fd >= 0) {
	    int on = 1;

	    if (term_cfd >= 0)
		close(term_cfd);
	    term_cfd = fd;
	    fcntl(term_cfd, F_SETFL, fcntl(term_cfd, F_GETFL, 0) | O_NONBLOCK);
	    setsockopt(term_cfd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
	    csp_line_prompt(&st->line);
	    csp_flush();
	}
    }
    if (term_cfd < 0)
	return;
    // Drain what is there and no more: this runs once per simulation step and
    // must never stall it -- the drone is in the air while you type.
    while (read(term_cfd, &c, 1) == 1)
	csp_con_input(st, c);
}

// FILE OUTPUT, the same hook csp_linux.c has. `/list > file` and the dump
// routines redirect through it; with the compiler linked in, this node has both
// of those. NULL means "print to the console", which is the socket above.
static FILE* file_output = NULL;

void* csp_set_file_output(void* f)
{
    void* prev = file_output;

    file_output = (FILE*)f;
    return prev;
}

int csp_will_output(void)
{
    return file_output != NULL;
}

// /save reports the store by name, and the store is a file -- see below.
static const char* eeprom_file;          // defined below, with the store

const char* csp_eeprom_name(void)
{
    return eeprom_file;
}

// What the parser reports when a rule is too deep to compile. A host stack is
// megabytes and this is a diagnostic, not a limit anything here enforces.
int stack_used(void)
{
    return 0;
}

int csp_print_char(char c)
{
    if (file_output) {
	fputc(c, (FILE*)file_output);
	return 1;
    }
    if (term_cfd >= 0) {
	if (write(term_cfd, &c, 1) == 1)
	    return 1;
	// The far end went away mid-line. Drop it and keep printing to the log
	// rather than losing the rest of whatever was being said.
	close(term_cfd);
	term_cfd = -1;
    }
    putchar(c);
    return 1;
}

// Both of these go through csp_print_char and not through printf, for the
// reason port/csp_linux.c gives at length: it is the node's single output
// point, and the console tap hangs off it. A rostring is read with ro_byte
// because under CSP_RO_POISON that is a second address space -- having the
// host read one the same way an AVR does is what keeps the poison honest.
int csp_print_str(const char* s)
{
    int n = 0;

    while (*s)
	n += csp_print_char(*s++);
    return n;
}

int csp_print_rostr(rostring_t s)
{
    const uint8_t* p = (const uint8_t*)s;
    int n = 0;
    uint8_t c;

    while ((c = ro_byte(p + n)) != 0) {
	csp_print_char((char)c);
	n++;
    }
    return n;
}

void csp_flush(void)
{
    if (file_output)
	fflush((FILE*)file_output);
    fflush(stdout);
}

// ------------------------------------------------------------------ absent hardware
//
// THE GROUND LINK IS THE KEYBOARD.
//
// pilot declares `#buffer Command:16 in can 0x20` -- on the real drone that is
// Enhanced ShockBurst from a Crazyradio, relayed by the nRF51 over syslink, and
// pins/link.csp says it is declared as a CAN frame because that is what it is
// in every way the logic cares about. So it is here too: a key press becomes
// frame 0x20 with the command in byte 0, and takes the same path a radio packet
// would. The program cannot tell, and tests/command.dat drives the identical
// path from a file.
//
//     g   CMD_GO      arm and fly to TARGET
//     l   CMD_LAND    descend and disarm
//     a   CMD_ABORT   cut the motors now
//
// ON THE EDGE, not while held. Webots reports a key EVERY step it is down, and
// a command repeated 250 times a second is not what a radio does -- it is what
// makes `? Cmd == CMD_GO` fire on a state that has already moved on.
int csp_can_recv(csp_rt_t* st, uint32_t* id, uint8_t* data, uint8_t* len)
{
    static int last_key = -1;
    int k;
    uint8_t cmd = 0;

    (void)st;
    // THE REAL BUS FIRST. With --can=vcan0 in controllerArgs this drone sits on
    // a SocketCAN interface like any other node, and anything that can put a
    // frame on it can fly it -- `cansend vcan0 020#01`, or a wx app in Erlang.
    // The keyboard stays as the thing that needs nothing set up.
    {
	int r = csp_socketcan_recv(id, data, len);

	if (r != 0)
	    return r;
    }
    k = wb_keyboard_get_key();
    // DRAIN the rest of this step's keys. Webots queues them, and leaving them
    // there means the next cycle acts on a press that is already over.
    while (wb_keyboard_get_key() != -1)
	;
    // ON THE DOWN EDGE ONLY, and the test is not `k != last_key`. Webots does
    // not report a held key steadily -- it comes back as g, -1, g, -1 -- so a
    // change test fires on every one of those, and the log filled with
    // `link: cmd 1` while the state machine was handed GO over and over.
    // A press is a key arriving after NOTHING.
    {
	int prev = last_key;

	last_key = k;
	if ((k == -1) || (prev != -1))
	    return 0;
    }
    switch (k) {
    case 'g': case 'G': cmd = CMD_GO;    break;
    case 'l': case 'L': cmd = CMD_LAND;  break;
    case 'a': case 'A': cmd = CMD_ABORT; break;
    default: return 0;
    }
    *id = 0x20;
    memset(data, 0, 8);
    data[0] = cmd;
    *len = 8;
    csp_print_lit("link: cmd ");
    csp_print_uint(cmd);
    csp_println();
    csp_flush();
    return 1;
}

int csp_can_send(csp_rt_t* st, uint32_t id, const uint8_t* data, uint8_t len)
{
    (void)st;
    return csp_socketcan_send(id, data, len);
}

// A FILE, the same way ./csp backs one. It was a set of stubs at first, on the
// grounds that reverting the world is what "reboot" means here -- but that also
// meant /save had nowhere to go, and a parameter trimmed from the prompt was
// lost the moment the world reloaded. A simulated node that cannot keep what
// you tuned is a worse model of a real one than a file is.
//
// Beside the controller, not in the working directory: Webots runs a controller
// with its own directory as cwd, so this lands in
//
//     private/pilot/webots/controllers/pilot/eeprom.db
//
// --eeprom=PATH moves it, which is how two worlds get separate stores.
static const char* eeprom_file = "eeprom.db";
static FILE* eeprom_fp = NULL;

int csp_eeprom_open_read(void)
{
    eeprom_fp = fopen(eeprom_file, "rb");
    return eeprom_fp ? 0 : -1;
}

int csp_eeprom_open_write(void)
{
    eeprom_fp = fopen(eeprom_file, "wb");
    return eeprom_fp ? 0 : -1;
}

void csp_eeprom_close(void)
{
    if (eeprom_fp) {
	fclose(eeprom_fp);
	eeprom_fp = NULL;
    }
}

int csp_eeprom_read(void* b, size_t n)
{
    if (!eeprom_fp)
	return -1;
    return (fread(b, 1, n, eeprom_fp) == n) ? 0 : -1;
}

int csp_eeprom_write(const void* b, size_t n)
{
    if (!eeprom_fp)
	return -1;
    return (fwrite(b, 1, n, eeprom_fp) == n) ? 0 : -1;
}

// A plain file has no ceiling. The Crazyflie's real store is the nRF51's, which
// this program never touches -- so there is no board figure to imitate.
uint32_t csp_eeprom_capacity(void)
{
    return CSP_EEPROM_UNBOUNDED;
}

// No second image to boot into either: /upgrade is a node-in-the-field feature
// and this node is reverted, not upgraded.
int csp_flash_read(uint32_t a, void* b, uint32_t n)
{
    (void)a; (void)b; (void)n;
    return -1;
}

int csp_flash_write(uint32_t a, const void* b, uint32_t n)
{
    (void)a; (void)b; (void)n;
    return -1;
}

int csp_flash_erase(uint32_t a, uint32_t n)
{
    (void)a; (void)n;
    return -1;
}

uint32_t csp_time_ms(void)
{
    return (uint32_t)(wb_robot_get_time() * 1000.0);
}

unsigned long csp_time_us(void)
{
    return (unsigned long)(wb_robot_get_time() * 1000000.0);
}

// ------------------------------------------------------------------ pins

// A DECLARED PIN IS A DEVICE INDEX HERE. pilot says `M1 out pwm 0:1`, and 0:1
// is a port and a pin on an STM32; in this world it is simply which motor.
// The mapping is by ORDER of declaration, which is how the board terms list
// them -- the alternative is to teach the port about port/pin numbers that
// describe silicon this robot does not have.
static index_t motor_ix[4] = { BAD_INDEX, BAD_INDEX, BAD_INDEX, BAD_INDEX };
static int     motor_n = 0;
static double  last_w[4];                // per motor, for the trace below

void csp_board_digital_config(value_t* vptr)
{
    (void)vptr;                          // no pin to configure: a LED is a LED
}

void csp_board_digital_input(csp_rt_t* st, index_t ix, value_t* vptr)
{
    (void)st; (void)ix; (void)vptr;      // nothing reads a pin in this world
}

void csp_board_digital_output(csp_rt_t* st, value_t* vptr)
{
    (void)st; (void)vptr;                // Fault/Armed: state, not light
}

void csp_board_analog_config(value_t* vptr)
{
    (void)vptr;
}

// The battery, and it is the one reading with no device behind it: the model
// has no cell to measure. A constant is honest -- pilot's low-battery cutoff
// is tested by tests/lowbat.dat, which drives Vraw directly.
void csp_board_analog_input(csp_rt_t* st, index_t ix, value_t* vptr)
{
    (void)vptr;
    csp_set_ivalue(st, ix, 2800);        // 4.1 V through VBAT_SCALE
}

// pilot's mixer works in normalised thrust and clamps at 32767 (motors.csp says
// why it is 15 bits and not 16); the model's motors take rad/s. See
// MOTOR_MAX_RADS above -- that is the knob.
void csp_board_analog_output(csp_rt_t* st, int di, value_t* vptr)
{
    int i;

    for (i = 0; i < motor_n; i++) {
	if (motor_ix[i] != (index_t)di)
	    continue;
	{
	    double u = (double)value_get_a_val(vptr) / 32767.0;
	    double w;

	    if (u < 0.0) u = 0.0;
	    if (u > 1.0) u = 1.0;
	    w = sqrt(u) * MOTOR_MAX_RADS;
	    last_w[i] = w;
	    wb_motor_set_velocity(dev_motor[i], motor_dir[i] * w);
	}
	return;
    }
    (void)st;
}

// ------------------------------------------------------------------ frames

// A BUFFER BY NAME. The sensors are #buffer declarations with no transport --
// nothing polls them, so this port is what fills them, and it has to find them
// the way a person would: by what they are called.
static index_t buf_by_name(csp_rt_t* st, const char* s)
{
    tstr_t  t;
    index_t di;

    t.ptr = (char*)s;
    t.len = (int)strlen(s);
    di = csp_lookup_decl(st, &t);
    if (di == BAD_INDEX)
	return BAD_INDEX;
    return csp_buf_of_decl(st, INDEX(di));
}

static index_t buf_imu = BAD_INDEX;
static index_t buf_flow = BAD_INDEX;
static index_t buf_tof = BAD_INDEX;

// THE SAME PATH A CAN FRAME TAKES. csp_buf_deliver puts the bytes in the SHADOW
// half, raises RXPEND for the commit to turn into `.rx`, and marks the fields --
// writing the committed half instead would leave nothing to compare against, so
// every field would read as unchanged and the reactive half of the program would
// never see the sensor move.
static void deliver(csp_rt_t* st, index_t b, const uint8_t* data, uint16_t n)
{
    if (b != BAD_INDEX)
	csp_buf_deliver(st, b, data, n);
}

// MSB first, which is what `big` in imu.csp means -- and what makes this worth
// doing rather than writing the fields: the unpacking in that file has only
// ever run against frames a test wrote by hand.
static void be16(uint8_t* p, double v)
{
    int32_t i = (int32_t)v;

    if (i >  32767) i =  32767;
    if (i < -32768) i = -32768;
    p[0] = (uint8_t)((uint16_t)i >> 8);
    p[1] = (uint8_t)((uint16_t)i & 0xFF);
}

static void le16(uint8_t* p, double v)
{
    int32_t i = (int32_t)v;

    if (i >  32767) i =  32767;
    if (i < -32768) i = -32768;
    p[0] = (uint8_t)((uint16_t)i & 0xFF);
    p[1] = (uint8_t)((uint16_t)i >> 8);
}

// MPU9250 register 0x3B onwards: Ax Ay Az temp Gx Gy Gz, seven big-endian
// int16. Az and the temperature are in the frame and not declared -- see
// imu.csp -- so they are filled anyway, because the frame is the frame.
static void read_imu(csp_rt_t* st)
{
    const double* a;
    const double* g;
    uint8_t f[14];

    // NOTHING RATHER THAN ZEROS. An absent device reads as a level, motionless
    // drone -- which the complementary filter believes, and which is the worst
    // possible answer: the program flies on a lie instead of noticing it has no
    // sensor. Delivering no frame leaves `.rx` low, and a rule guarded on it
    // simply does not run.
    if ((dev_acc == 0) || (dev_gyro == 0))
	return;
    a = wb_accelerometer_get_values(dev_acc);
    g = wb_gyro_get_values(dev_gyro);
    if (!a || !g)
	return;
    be16(&f[0],  a[0] * ACC_LSB_PER_MS2);
    be16(&f[2],  a[1] * ACC_LSB_PER_MS2);
    be16(&f[4],  a[2] * ACC_LSB_PER_MS2);
    be16(&f[6],  0.0);                   // temperature: nothing reads it
    be16(&f[8],  g[0] * GYRO_LSB_PER_RADS);
    be16(&f[10], g[1] * GYRO_LSB_PER_RADS);
    be16(&f[12], g[2] * GYRO_LSB_PER_RADS);
    deliver(st, buf_imu, f, sizeof(f));
}

// VL53L1x: height in millimetres, little-endian. The model's rangefinder
// answers in metres along its own axis, which is straight down.
// HEIGHT COMES FROM THE GPS, not from a simulated ray, and that is a decision
// rather than a shortcut.
//
// IT IS NOT THE SENSOR TYPE. A laser really does struggle close to a surface --
// the VL53L1x has a 10 mm dead zone, which is why TOF_MIN_MM exists -- and a
// sonar would be the honest answer to that problem. But here the ray never
// reached the floor at all: the Crazyflie PROTO's extensionSlot sits inside
// `DEF BODY Pose { translation 0 0 -0.015 }`, the underside of the hull, and
// the drone rests with its origin at z = 0.015. The sensor therefore started
// its ray IN THE FLOOR PLANE. It answered 2000 -- the lookupTable maximum,
// meaning "nothing in range" -- while she sat on the ground, and only began
// reading at 202 mm once she was already climbing. A sonar at the same point
// sees exactly as little.
//
// Nor is there room to move it: below is under the floor, above is back inside
// the hull, whose bounding cylinder spans the whole 3 cm. A real ray would want
// the sensor out past the 5 cm hull radius on an arm -- which is Webots
// geometry, not the program.
//
// What is EMULATED is still the SENSOR: the frame, the units, the byte order
// and the range behaviour. flow.csp calls anything outside 10..1300 mm a lie,
// and this hands the program nothing it has not earned.
static void read_tof(csp_rt_t* st)
{
    const double* p;
    double  mm;
    uint8_t f[2];

    if (dev_gps == 0)
	return;                          // see read_imu
    if ((p = wb_gps_get_values(dev_gps)) == NULL)
	return;
    mm = p[2] * 1000.0;
    if (mm < 0.0)
	mm = 0.0;
    if (mm > 2000.0)
	mm = 2000.0;                     // past the ceiling: out of range either way
    le16(&f[0], mm);
    deliver(st, buf_tof, f, sizeof(f));
}

// PMW3901: PIXELS MOVED since the last read, not a position -- which is why
// this keeps the previous one. The sensor measures an ANGLE, so the same
// ground travel is fewer pixels the higher you are; pilot multiplies by height
// to undo exactly this, and the inverse is what turns a simulated velocity
// back into something the real lens would have reported.
static void read_flow(csp_rt_t* st)
{
    static double px = 0.0, py = 0.0;
    static int    have = 0;
    const double* p;
    double        alt;
    uint8_t       f[4];

    if (dev_gps == 0)
	return;                          // see read_imu
    if ((p = wb_gps_get_values(dev_gps)) == NULL)
	return;
    alt = p[2];
    if (alt < 0.05)                      // below focus: the lens reports noise
	alt = 0.05;
    if (!have) {
	px = p[0]; py = p[1]; have = 1;
    }
    // FLOW_RAD_PER_PX is 0.0128 (flow.csp). A metre of travel at one metre of
    // height is 1/0.0128 pixels; at two metres it is half that.
    le16(&f[0], (p[0] - px) / (0.0128 * alt));
    le16(&f[2], (p[1] - py) / (0.0128 * alt));
    px = p[0];
    py = p[1];
    deliver(st, buf_flow, f, sizeof(f));
}

// ------------------------------------------------------------------ tracing

// EVERY STATE CHANGE, BY NAME. A drone that will not answer a command is most
// often a drone that is not in the state the command is handled in --
// main.csp's LAND lives in `#in flying` and nowhere else -- and from the
// outside that is indistinguishable from a command that never arrived.
//
// Declaration 0 is always State. Printed on the change and not per cycle: at
// 250 Hz a line per cycle is not a trace, it is a wall.
static void trace_state(csp_rt_t* st)
{
    static int last = -1;
    int s = (int)csp_value(st, 0).i;
    sindex_t pos;

    if (s == last)
	return;
    last = s;
    csp_print_lit("state: ");
    if ((pos = (sindex_t)csp_state_name_at(st, (index_t)s)) != 0)
	csp_print_str_at(st, pos);
    else
	csp_print_uint((uint32_t)s);     // INIT/NORMAL/FAILSAFE have no slot
    csp_println();
    csp_flush();
}

// A LINE A SECOND, from the port rather than the prompt: at 250 Hz nobody can
// type fast enough to watch a climb, and by the time you have asked, it is
// over. Off by default -- build with -DCSP_WEBOTS_TRACE=N for a line every N
// cycles (250 is one a second).
//
// What it prints is the altitude chain end to end, which is what says WHERE a
// climb stops being controlled:
//
//   mm     what the rangefinder reported, BEFORE the program judged it
//   alt    what the estimator believes -- frozen if mm left 10..1300, because
//          flow.csp calls a reading outside that a lie and stops updating
//   thr    commanded thrust, 0.05..1.0
//   w      what that became in rad/s, so the mapping is visible too
// EVERY N CYCLES, and N comes from the COMMAND LINE rather than from a -D.
// A build flag means rebuilding to look at something, and by then the flight
// you wanted to see is over:
//
//     Crazyflie { controllerArgs [ "--trace=125" ] }     two lines a second
//     controllerArgs [ "--can=vcan0" "--trace=250" ]     both
//
// 0 (the default) is off, and costs a compare per cycle on a host.
static long trace_every = 0;

static void trace_flight(csp_rt_t* st)
{
    static long n = 0;
    index_t di;
    tstr_t t;

    if ((trace_every <= 0) || ((n++ % trace_every) != 0))
	return;
    csp_print_lit("t: mm ");
    csp_print_uint((uint32_t)(dev_gps ? wb_gps_get_values(dev_gps)[2] * 1000.0
				      : 0.0));
    t.ptr = (char*)"Alt"; t.len = 3;
    if ((di = csp_lookup_decl(st, &t)) != BAD_INDEX) {
	csp_print_lit("  alt ");
	csp_print_value(st, V_FLOAT, csp_value(st, di));
    }
    t.ptr = (char*)"Thrust"; t.len = 6;
    if ((di = csp_lookup_decl(st, &t)) != BAD_INDEX) {
	csp_print_lit("  thr ");
	csp_print_value(st, V_FLOAT, csp_value(st, di));
    }
    // ALL FOUR, not one. A drone that will not leave the ground with 1.3x its
    // weight commanded is a drone where some of that command is not arriving,
    // and a single number cannot show that.
    csp_print_lit("  w");
    {
	int i;

	for (i = 0; i < 4; i++) {
	    csp_print_char(' ');
	    csp_print_uint((uint32_t)last_w[i]);
	}
	csp_print_lit("  n ");
	csp_print_uint((uint32_t)motor_n);
    }
    csp_println();
    csp_flush();
}

// ------------------------------------------------------------------ the sweeps

void csp_setup(csp_rt_t* st)
{
    index_t i;
    int     k = 0;

    // The motors, in declaration order. st->io holds every #digital and
    // #analog the program declared, in the order it declared them, which is
    // the order the board terms list the devices in.
    for (i = 0; i < st->nio && k < 4; i++) {
	index_t ix = csp_io_at(st, i);

	if (decl(st, INDEX(ix), type) == DECL_ANALOG &&
	    (decl(st, INDEX(ix), dir) & DIR_OUT))
	    motor_ix[k++] = INDEX(ix);
    }
    motor_n = k;

    buf_imu  = buf_by_name(st, "Imu");
    buf_flow = buf_by_name(st, "Flow");
    buf_tof  = buf_by_name(st, "Tof");

    csp_setup_events(st);
}

void csp_input(csp_rt_t* st)
{
    int i;

    // The analog INPUTS first -- the battery is the only one, and it is a
    // constant. Digital inputs have no pins in this world.
    for (i = 0; i < st->nio; i++) {
	index_t ix = csp_io_at(st, i);

	if ((decl(st, INDEX(ix), type) == DECL_ANALOG) &&
	    (decl(st, INDEX(ix), dir) & DIR_IN))
	    csp_board_analog_input(st, ix, csp_dio_slot(st, ix, DOUT));
    }
    csp_ctx_reset(st);

    read_imu(st);
    read_tof(st);
    read_flow(st);

    // THE GROUND LINK, and it is not optional: csp_can_recv is where a key
    // press becomes frame 0x20, and without this call nothing ever asks. The
    // drone sat in preflight with the keyboard enabled and every command
    // dropped on the floor.
    csp_can_input(st);
    csp_buf_input(st);
    csp_input_timer(st);
    csp_input_event(st);
}

void csp_output(csp_rt_t* st)
{
    int i;

    if (!st->latch) {
	for (i = 0; i < st->nio; ++i) {
	    index_t ix = csp_io_at(st, i);

	    if ((decl(st, INDEX(ix), type) == DECL_ANALOG) &&
		(decl(st, INDEX(ix), dir) & DIR_OUT))
		csp_board_analog_output(st, INDEX(ix),
					csp_dio_slot(st, ix, DOUT));
	}
	csp_ctx_reset(st);
	csp_can_output(st);
	csp_buf_output(st);
    }
    csp_output_timer(st);
}

// ------------------------------------------------------------------ board

// A DEVICE, OR A COMPLAINT. wb_robot_get_device answers 0 for a name the model
// does not carry, and a tag of 0 is not an error to any of the read calls --
// they simply answer nothing, for the life of the run. So a missing sensor is a
// program whose accelerometer reads zero forever, which looks exactly like a
// drone sitting still.
//
// Two of these come from the extensionSlot in pilot.wbt and are the ones most
// likely to be absent -- an older PROTO, a world built from the stock
// crazyflie.wbt, a name typed differently. Say so once, at boot, and list what
// the model DOES have, because that is the answer to "then what is it called".
static WbDeviceTag need(const char* name)
{
    WbDeviceTag t = wb_robot_get_device(name);

    if (t == 0) {
	csp_print_lit("dev: MISSING ");
	csp_print_str(name);
	csp_println();
	csp_flush();
    }
    return t;
}

static void list_devices(void)
{
    int n = wb_robot_get_number_of_devices();
    int i;

    csp_print_lit("dev: model has");
    for (i = 0; i < n; i++) {
	csp_print_char(' ');
	csp_print_str(wb_device_get_name(wb_robot_get_device_by_index(i)));
    }
    csp_println();
    csp_flush();
}

void csp_board_init(void)
{
    wb_robot_init();
    timestep = (int)wb_robot_get_basic_time_step();

    list_devices();
    dev_motor[0] = need("m1_motor");
    dev_motor[1] = need("m2_motor");
    dev_motor[2] = need("m3_motor");
    dev_motor[3] = need("m4_motor");
    {
	int i;
	for (i = 0; i < 4; i++) {
	    // Velocity control, not position: INFINITY is how Webots is told
	    // that this joint spins rather than seeks.
	    wb_motor_set_position(dev_motor[i], INFINITY);
	    wb_motor_set_velocity(dev_motor[i], 0.0);
	}
    }

    dev_gyro = need("gyro");
    dev_acc  = need("accelerometer");    // extensionSlot, pilot.wbt
    // read_tof does not use it any more, so look it up without complaining: a
    // world that still carries one should not warn on every boot.
    dev_tof  = wb_robot_get_device("range_down");
    dev_gps  = need("gps");
    if (dev_gyro) wb_gyro_enable(dev_gyro, timestep);
    if (dev_acc)  wb_accelerometer_enable(dev_acc, timestep);
    if (dev_tof)  wb_distance_sensor_enable(dev_tof, timestep);
    if (dev_gps)  wb_gps_enable(dev_gps, timestep);

    // The ground link. Webots delivers keys to the controller only while the
    // 3D view has focus -- click the drone before typing.
    wb_keyboard_enable(timestep);
}

void csp_board_setup(csp_rt_t* st)        { (void)st; }
void csp_board_start_input(csp_rt_t* st)  { (void)st; }
void csp_board_start_output(csp_rt_t* st) { (void)st; }
void csp_board_stop_output(csp_rt_t* st)  { (void)st; }

// THE POOL IS SIZED FROM THIS, and 0 is not "unknown" -- it is "no room", which
// is what csp_rt_init reported. With CSP_ARENA_MALLOC csp_mem_init asks
// csp_system_ram_avail() how much to claim, so a port that answers nothing gets
// an arena of nothing and the program does not fit before it is loaded.
//
// SYSTEM_RAM_CAPACITY is the host figure csp_linux.c uses (256K). A simulated
// drone has no RAM budget worth modelling: the point of this board is the
// SENSORS, and a pool that runs out here would only mean this file lied about
// a machine that does not exist. Model the flash budget on crazyflie, which is
// the board that has one.
uint32_t csp_system_ram_capacity(void) { return SYSTEM_RAM_CAPACITY; }
uint32_t csp_system_ram_used(void)     { return 0; }
uint32_t csp_system_ram_avail(void)    { return csp_system_ram_capacity(); }

int csp_board_irq_attach(csp_rt_t* st, index_t ix, trigger_t trig, uint8_t slot)
{
    (void)st; (void)ix; (void)trig; (void)slot;
    return -1;                           // no edges in this world
}

uint32_t csp_board_irq_take(csp_rt_t* st)
{
    (void)st;
    return 0;
}

// ------------------------------------------------------------------ main

static csp_rt_t state;

// WEBOTS PASSES controllerArgs STRAIGHT TO argv, so the world file is where a
// bus gets named:
//
//     Crazyflie { controller "pilot"  controllerArgs [ "--can=vcan0" ] }
//
// Same spelling as ./csp's own --can, because it is the same question.
static const char* arg_val(int argc, char** argv, const char* key)
{
    size_t n = strlen(key);
    int i;

    for (i = 1; i < argc; i++)
	if (strncmp(argv[i], key, n) == 0)
	    return argv[i] + n;
    return NULL;
}

// --set=Name=Value: a PARAMETER FROM THE WORLD FILE.
//
// A #param is patched through the settings store on a real node, and this one
// has no store -- reverting the world is what "reboot" means here. But the
// numbers a simulated airframe needs are exactly the ones that differ from the
// hardware: RampUp is 0.4 on a Crazyflie and 0.1 on this model, because the
// model accelerates a fiftieth of g just above hover and the real one does not.
//
//     controllerArgs [ "--set=RampUp=0.1" ]
//
// Applied AFTER csp_rebuild, so it lands on the running program rather than on
// a table that is about to be laid out again. Anything the program declares can
// be set; a name it does not know is reported rather than ignored, because a
// misspelt parameter is a flight that silently uses the default.
static void apply_set(csp_rt_t* st, const char* spec)
{
    char    name[32];
    const char* eq = strchr(spec, '=');
    value_t v;
    index_t di;
    tstr_t  t;
    size_t  n;

    if (eq == NULL) {
	csp_print_lit("set: no '=' in ");
	csp_print_str(spec);
	csp_println();
	return;
    }
    n = (size_t)(eq - spec);
    if (n >= sizeof(name))
	n = sizeof(name) - 1;
    memcpy(name, spec, n);
    name[n] = '\0';
    t.ptr = name;
    t.len = (int)n;
    if ((di = csp_lookup_decl(st, &t)) == BAD_INDEX) {
	csp_print_lit("set: no such name ");
	csp_print_str(name);
	csp_println();
	return;
    }
    // The declared type decides how the text is read: a float parameter takes
    // 0.1 and an integer one takes 10, and handing the wrong one over is how a
    // tunable silently becomes zero. On a fixpoint build a float is Q16.16, so
    // the conversion is a scale and not a cast -- `(fvalue_t)0.1` there is 0.
    if (decl(st, INDEX(di), vt) == V_FLOAT) {
	double d = atof(eq + 1);

#if FVALUE_IS_FIXPOINT
	v.f = (fvalue_t)(d * 65536.0 + (d >= 0 ? 0.5 : -0.5));
#else
	v.f = (fvalue_t)d;
#endif
    }
    else
	v.i = (ivalue_t)atol(eq + 1);
    csp_set_value(st, di, v);
    csp_print_lit("set: ");
    csp_print_str(name);
    csp_print_lit(" = ");
    csp_print_str(eq + 1);
    csp_println();
}

int main(int argc, char** argv)
{
    const char* iface = arg_val(argc, argv, "--can=");
    const char* tr    = arg_val(argc, argv, "--trace=");
    const char* sp    = arg_val(argc, argv, "--spin=");
    const char* ee    = arg_val(argc, argv, "--eeprom=");

    if (ee && *ee)
	eeprom_file = ee;

    if (tr)
	trace_every = atol(tr);
    csp_board_init();

    // --spin=N: DRIVE THE ROTORS AND NOTHING ELSE. The program does not run,
    // nothing is read, the four motors turn at N rad/s for as long as the world
    // does. It answers one question that argument cannot -- at what speed does
    // THIS model actually leave the floor -- and it answers it in one run
    // instead of through a controller that is also ramping, estimating and
    // regulating.
    //
    //     controllerArgs [ "--spin=56" ]     the figure the constants predict
    //     controllerArgs [ "--spin=81" ]     where she was observed to lift
    //
    // 4 * 4e-05 * w^2 against 0.05 kg * 9.81 puts hover at 55.4 rad/s. If she
    // sits still at 60 and climbs at 80, half the lift is missing and the
    // question is Webots' propellers, not this port.
    if (sp) {
	double w = atof(sp);
	int i;

	csp_print_lit("spin: ");
	csp_print_uint((uint32_t)w);
	csp_print_line(" rad/s on all four -- the program is NOT running");
	csp_flush();
	for (i = 0; i < 4; i++)
	    wb_motor_set_velocity(dev_motor[i], motor_dir[i] * w);
	while (wb_robot_step(timestep) != -1) {
	    static int n = 0;

	    if ((n++ % 250) == 0) {
		const double* p = dev_gps ? wb_gps_get_values(dev_gps) : NULL;

		csp_print_lit("spin: mm ");
		csp_print_uint((uint32_t)(p ? p[2] * 1000.0 : 0.0));
		csp_println();
		csp_flush();
	    }
	}
	wb_robot_cleanup();
	return 0;
    }
    // Before the runtime: a bus that will not open is worth knowing about while
    // there is still a console to say it on.
    if (iface && (csp_socketcan_open(iface) == 0)) {
	csp_print_lit("link: can ");
	csp_print_str(iface);
	csp_println();
	csp_flush();
    }
    // CSP_CSTATE, not 0. The third argument is the COMPILER's state, and this
    // build has one -- the prompt on 2323 is the whole point. With NULL here
    // csp_parse's first statement is `st->cs->ap = &alloc;`, so the node comes
    // up, flies, answers /state, and dies the moment anything is DECLARED at
    // it. port/csp_avr.c carries the same note for the same reason.
    if (csp_rt_init(&state, REACTIVE_DEFAULT, CSP_CSTATE) < 0) {
	csp_print_line("FATAL: csp_rt_init failed");
	csp_flush();
	return 1;
    }
    // WHICH image, before one is loaded: sys.Boot lives in the settings store, so
    // the store is read on its own first. Same order as every other port.
    if (csp_eeprom_peek(&state) == 0)
	csp_boot_pick(&state);

    csp_load_rom(&state);

    // AND THEN THE PATCH. This was missing, and the symptom was quiet: /save
    // wrote its bytes, the file grew, and the next run came up on the linked
    // image as if nothing had been saved. A store that is written and never
    // read is worse than no store at all.
    //
    // csp_clr_error on failure is not cosmetic: "no saved state" is the NORMAL
    // case, and csp_set_error keeps the FIRST error -- so an uncleared
    // ERR_CANNOT_LOAD is carried into every command that follows.
    if (csp_eeprom_load(&state) != 0)
	csp_clr_error(&state);

    if (csp_rebuild(&state) < 0) {
	csp_print_line("FATAL: csp_rebuild failed -- program does not fit");
	csp_flush();
	return 1;
    }
    csp_setup(&state);
    state.latch = 0;
    {
	int i;

	for (i = 1; i < argc; i++)
	    if (strncmp(argv[i], "--set=", 6) == 0)
		apply_set(&state, argv[i] + 6);
	csp_flush();
    }
    csp_line_init(&state.line);
    term_listen();
    csp_print_lit("csp webots -- ");
    csp_print_uint((uint32_t)timestep);
    csp_print_line(" ms/step");
    csp_flush();

    // WEBOTS OWNS THE CLOCK. wb_robot_step returns -1 when the simulation is
    // reverted or quit, and that is the only way out of here -- the loop has
    // no other exit, the same way a board's does not.
    if (term_lfd >= 0) {
	csp_print_lit("term: telnet localhost ");
	csp_print_uint((uint32_t)CSP_WEBOTS_PORT);
	csp_println();
	csp_flush();
    }

    while (wb_robot_step(timestep) != -1) {
	// THE PROMPT FIRST, and a whole line is run before the cycle that
	// follows -- so a rule typed in is in force from the very next step
	// rather than one behind it.
	term_poll(&state);
	if (state.line.ready) {
	    csp_process_line(&state, state.line.buf);
	    csp_line_done(&state.line);
	    csp_line_prompt(&state.line);
	    csp_flush();
	}
	state.cycle++;
	csp_input(&state);
	csp_cycle(&state);
	csp_commit(&state);
	csp_output(&state);
	trace_state(&state);
	trace_flight(&state);
    }
    wb_robot_cleanup();
    return 0;
}
