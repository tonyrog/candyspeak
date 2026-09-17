// Webots' controller API, stubbed, so the PORT can be run without a simulator.
//
// port/csp_webots.c has a boot sequence in it -- csp_rt_init, csp_load_rom,
// csp_rebuild, csp_setup -- and every one of those can refuse. Finding that out
// by starting Webots, watching a controller exit with status 1 and reading the
// log is a slow way to learn that an arena came back empty (which is exactly
// how csp_system_ram_avail() returning 0 was found).
//
// So: link the port against these instead, run it, and the same boot runs with
// the same image. What it cannot test is the physics -- the drone does not fly
// here, the gyro reads zero and the motors go nowhere. What it DOES test is
// that the program loads, the buffers are found by name, and a frame delivered
// by the port reaches the fields the program reads.
//
// wb_robot_step counts down and then says -1, which is what Webots says when
// the world is reverted -- so main's loop ends the way it really does.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef int WbDeviceTag;

// LONG ENOUGH FOR THE COUNTDOWN. main.csp arms for CountdownS seconds (10) at
// RATE 250, so `flying` is 2500 cycles after the GO -- a budget of 200 ends the
// run while the drone is still counting, with the motors correctly at zero and
// nothing to show for it.
static int step_budget = 4000;     // 16 s, then the "world" is reverted

// TWO KNOBS, from the environment, so the same binary is both a check and a
// place to sit and type at:
//
//   CSP_STUB_STEPS=0     run forever -- what you want when connecting to the
//                        prompt on 2323, since the check's 4000 steps are over
//                        before telnet has finished its handshake
//   CSP_STUB_REALTIME=1  sleep the step out instead of running flat out, so
//                        the drone's clock matches yours while you type
static int stub_realtime = 0;
static int stub_env_read = 0;

static void stub_env(void)
{
    const char* v;

    stub_env_read = 1;
    if ((v = getenv("CSP_STUB_STEPS")) != NULL)
	step_budget = atoi(v);           // 0 or less: never runs out
    if ((v = getenv("CSP_STUB_REALTIME")) != NULL)
	stub_realtime = atoi(v);
}
static double sim_time = 0.0;
static const double TIMESTEP_MS = 4.0;

void wb_robot_init(void) { }

// WHAT THE RUN ACTUALLY DID, printed where the simulator would have shown it.
// A boot that completes and then drives nothing looks identical to a working
// one from the outside -- this is the difference, and it is what makes the
// stub a check rather than a smoke test.
double stub_motor[4];
static long stub_cycles = 0;

long stub_cycle_no(void) { return stub_cycles; }

void wb_robot_cleanup(void)
{
    printf("stub: %ld cycles, motors %.1f %.1f %.1f %.1f\n",
	   stub_cycles, stub_motor[0], stub_motor[1],
	   stub_motor[2], stub_motor[3]);
}
double wb_robot_get_basic_time_step(void) { return TIMESTEP_MS; }
double wb_robot_get_time(void) { return sim_time; }

int wb_robot_step(int ms)
{
    (void)ms;
    if (!stub_env_read)
	stub_env();
    stub_cycles++;
    // A budget of 0 or less is "no budget": the loop ends when the process is
    // killed, the way a real world ends when it is reverted.
    if ((step_budget > 0) && (--step_budget <= 0))
	return -1;
    if (stub_realtime)
	usleep((useconds_t)(TIMESTEP_MS * 1000.0));
    sim_time += TIMESTEP_MS / 1000.0;
    return 0;
}

// Every device resolves, so the port's lookups succeed and the interesting
// failures are the ones in the RUNTIME rather than in the model. Set
// CSP_STUB_NODEV to a name to make that one absent, which is how the port's
// "dev: MISSING" path is exercised without editing a world file.
WbDeviceTag wb_robot_get_device(const char* name)
{
    const char* gone = getenv("CSP_STUB_NODEV");

    if (gone && (strcmp(gone, name) == 0))
	return 0;
    return 1;
}

static const char* stub_dev[] = {
    "m1_motor", "m2_motor", "m3_motor", "m4_motor",
    "gyro", "accelerometer", "range_down", "gps", "camera"
};

int wb_robot_get_number_of_devices(void)
{
    return (int)(sizeof(stub_dev) / sizeof(stub_dev[0]));
}

WbDeviceTag wb_robot_get_device_by_index(int i) { return i + 1; }

const char* wb_device_get_name(WbDeviceTag t)
{
    int i = (int)t - 1;

    if ((i < 0) || (i >= wb_robot_get_number_of_devices()))
	return "?";
    return stub_dev[i];
}

void wb_motor_set_position(WbDeviceTag t, double p) { (void)t; (void)p; }

// The one output worth recording. A port that drives no motor at all is the
// failure this stub is most likely to catch, so the last commanded value is
// kept and main prints it at the end.
static int stub_motor_n = 0;
void wb_motor_set_velocity(WbDeviceTag t, double v)
{
    (void)t;
    stub_motor[stub_motor_n++ & 3] = v;
}

void wb_gyro_enable(WbDeviceTag t, int s) { (void)t; (void)s; }
void wb_accelerometer_enable(WbDeviceTag t, int s) { (void)t; (void)s; }
void wb_distance_sensor_enable(WbDeviceTag t, int s) { (void)t; (void)s; }
void wb_gps_enable(WbDeviceTag t, int s) { (void)t; (void)s; }
void wb_keyboard_enable(int s) { (void)s; }

// THE SCRIPTED FLIGHT. No one is at a keyboard here, so the stub presses the
// keys itself: go at cycle 20, land at 150.
//
// ONE KEY PER STEP, and the second call in the same step says -1 -- which is
// what Webots does, because the call DRAINS a queue. A stub that kept handing
// back the same key hung the port's own drain loop, which is a fair thing for
// a stub to have caught and a bad thing for it to have caused.
int wb_keyboard_get_key(void)
{
    extern long stub_cycle_no(void);
    static long served = -1;
    long c = stub_cycle_no();

    if (c == served)
	return -1;                     // this step's queue is empty
    if (c != 50 && c != 3200)
	return -1;
    served = c;
    return (c == 50) ? 'g' : 'l';
}

// LEVEL FLIGHT AT ONE METRE, unmoving. Not zero: an accelerometer at rest
// reads +1 g on Z, and a program whose complementary filter divides by G would
// divide by nothing if this said otherwise.
static const double stub_acc[3]  = { 0.0, 0.0, 9.81 };
static const double stub_gyro[3] = { 0.0, 0.0, 0.0 };
static const double stub_gps[3]  = { 0.0, 0.0, 1.0 };

const double* wb_accelerometer_get_values(WbDeviceTag t) { (void)t; return stub_acc; }
const double* wb_gyro_get_values(WbDeviceTag t) { (void)t; return stub_gyro; }
const double* wb_gps_get_values(WbDeviceTag t) { (void)t; return stub_gps; }
double wb_distance_sensor_get_value(WbDeviceTag t) { (void)t; return 1.0; }
