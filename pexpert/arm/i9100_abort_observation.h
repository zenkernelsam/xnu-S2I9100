/* Bounded UI-startup stderr and SIGABRT snapshots. Optional tccd observation
 * shares the SpringBoard slot. Backboard has a separate retained record.
 * PID/task checks prevent cross-attribution. No termination/permission policy. */
#ifndef I9100_ABORT_OBSERVATION_H
#define I9100_ABORT_OBSERVATION_H
#define I9100_ABORT_TEXT 240U
#define I9100_ABORT_FRAMES 8U
struct I9100AbortTrace {
    unsigned pid, task, pc, lr, sp, fp, count, error;
    unsigned frames[I9100_ABORT_FRAMES];
    unsigned slab, slab_object, slab_expected, slab_actual, slab_status;
};
struct I9100AbortObservation {
    unsigned sequence;
    struct I9100AbortTrace trace;
    unsigned text_kind;
    char text[I9100_ABORT_TEXT+1];
};
void ios7i9100_stderr_bytes(unsigned, unsigned, const char *, unsigned);
void ios7i9100_abort_publish(const struct I9100AbortTrace *);
void ios7i9100_backboard_stderr_bytes(unsigned, unsigned, const char *, unsigned);
void ios7i9100_backboard_abort_publish(const struct I9100AbortTrace *);

#ifdef I9100_ABORT_IMPLEMENTATION
struct I9100StderrBuffer {
    unsigned pid, task, head, used, length, critical, matched[3];
    char ring[512], line[I9100_ABORT_TEXT+1], diagnostic[I9100_ABORT_TEXT+1];
};
static struct I9100StderrBuffer i9100_stderr_buffer;
static struct I9100StderrBuffer i9100_backboard_stderr_buffer;
volatile struct I9100AbortObservation i9100_abort_observation;
volatile struct I9100AbortObservation i9100_backboard_abort_observation;

/* Raw IRQ/FIQ serialization is supplied by the board entry points. Matching
 * spans write/writev fragments. Keep the first diagnostic line, so a subsequent
 * stack dump or libc++abi footer cannot erase the exception's reason. */
static void i9100_stderr_append_to(struct I9100StderrBuffer *b, unsigned pid,
                                  unsigned task, const char *bytes, unsigned size)
{
    static const char *patterns[3] = {"uncaught exception", "assertion failed", "malloc:"};
    if (b->pid != pid || b->task != task) {
        b->pid = pid; b->task = task; b->head = b->used = b->length = b->critical = 0;
        b->diagnostic[0] = b->line[0] = 0;
        for (unsigned n = 0; n < 3; n++) b->matched[n] = 0;
    }
    if (size > 128) size = 128;
    for (unsigned n = 0; n < size; n++) {
        unsigned char c = (unsigned char)bytes[n];
        if (c != '\n' && (c < 32 || c > 126)) c = ' ';
        b->ring[b->head] = (char)c;
        b->head = (b->head+1U) & 511U;
        if (b->used < 512) b->used++;
        if (c != '\n' && b->length < I9100_ABORT_TEXT) {
            b->line[b->length++] = (char)c;
            b->line[b->length] = 0;
        }
        unsigned char lower = c >= 'A' && c <= 'Z' ? c + ('a'-'A') : c;
        for (unsigned k = 0; k < 3; k++) {
            const char *pattern = patterns[k];
            unsigned m = b->matched[k];
            m = lower == (unsigned char)pattern[m] ? m+1U :
                lower == (unsigned char)pattern[0] ? 1U : 0U;
            if (!pattern[m]) { b->critical = 1; m = 0; }
            b->matched[k] = m;
        }
        if ((c == '\n' || b->length == I9100_ABORT_TEXT) &&
            b->critical && !b->diagnostic[0]) {
            for (unsigned k = 0; k <= b->length; k++) b->diagnostic[k] = b->line[k];
        }
        if (c == '\n') {
            b->length = b->critical = 0; b->line[0] = 0;
            for (unsigned k = 0; k < 3; k++) b->matched[k] = 0;
        }
    }
}

static void i9100_stderr_append(unsigned pid, unsigned task, const char *bytes, unsigned size)
{
    i9100_stderr_append_to(&i9100_stderr_buffer, pid, task, bytes, size);
}

static void i9100_abort_store_to(volatile struct I9100AbortObservation *out,
                                struct I9100StderrBuffer *b,
                                const struct I9100AbortTrace *in)
{
    unsigned next = out->sequence + 1U;
    out->sequence = 0;
    out->trace.pid = in->pid; out->trace.task = in->task;
    out->trace.pc = in->pc; out->trace.lr = in->lr;
    out->trace.sp = in->sp; out->trace.fp = in->fp;
    out->trace.count = in->count; out->trace.error = in->error;
    for (unsigned n = 0; n < I9100_ABORT_FRAMES; n++) out->trace.frames[n] = in->frames[n];
    out->trace.slab = in->slab; out->trace.slab_object = in->slab_object;
    out->trace.slab_expected = in->slab_expected; out->trace.slab_actual = in->slab_actual;
    out->trace.slab_status = in->slab_status;
    out->text_kind = 0;
    unsigned used = 0;
    if (b->pid == in->pid && b->task == in->task) {
        const char *line = b->diagnostic[0] ? b->diagnostic : b->critical ? b->line : NULL;
        if (line) {
            out->text_kind = 1;
            for (; used < I9100_ABORT_TEXT && line[used]; used++) out->text[used] = line[used];
        } else {
            used = b->used < I9100_ABORT_TEXT ? b->used : I9100_ABORT_TEXT;
            out->text_kind = used ? 2 : 0;
            for (unsigned n = 0; n < used; n++) {
                char c = b->ring[(b->head+512U-used+n)&511U];
                out->text[n] = c == '\n' ? ' ' : c;
            }
        }
    }
    for (; used <= I9100_ABORT_TEXT; used++) out->text[used] = 0;
    out->sequence = next ? next : 1U;
}

static void i9100_abort_store(const struct I9100AbortTrace *in)
{
    i9100_abort_store_to(&i9100_abort_observation, &i9100_stderr_buffer, in);
}

/* Render only for the matching retained SIGABRT exit, after respawn as well. */
static int i9100_abort_line(unsigned row, unsigned pid, unsigned signal,
                             char *out, unsigned capacity)
{
    volatile struct I9100AbortObservation *r = &i9100_abort_observation;
    unsigned sequence = r->sequence;
    if (!sequence || r->trace.pid != pid || signal != 6 || row > 10) return 0;
    if (row >= 9 && !r->trace.slab_status) return 0;
    if (row == 0) snprintf(out, capacity, "ABORT PID%u PC%08x LR%08x", pid, r->trace.pc, r->trace.lr);
    else if (row == 1) snprintf(out, capacity, "SP%08x FP%08x N%u E%u", r->trace.sp, r->trace.fp, r->trace.count, r->trace.error);
    else if (row < 4) {
        unsigned base = (row-2U)*4U;
        snprintf(out, capacity, "BT%u %08x %08x %08x %08x", base,
            r->trace.frames[base], r->trace.frames[base+1], r->trace.frames[base+2], r->trace.frames[base+3]);
    } else if (row == 4) snprintf(out, capacity, "STDERR %s", r->text_kind == 1 ? "diagnostic line" : r->text_kind == 2 ? "last240 bytes" : "unavailable");
    else if (row == 9) snprintf(out, capacity, "SLAB%08x OBJ%08x B%u S%u", r->trace.slab,
        r->trace.slab_object, r->trace.slab_expected & 255U, r->trace.slab_status);
    else if (row == 10) snprintf(out, capacity, "MAGIC expected%08x actual%08x", r->trace.slab_expected, r->trace.slab_actual);
    else {
        char line[61];
        for (unsigned n = 0; n < 60; n++) line[n] = r->text[(row-5U)*60U+n];
        line[60] = 0;
        snprintf(out, capacity, "%s", line);
    }
    return sequence == r->sequence;
}
#endif
#endif
