#!/usr/bin/env python3
#
#  TessronOS
#  Copyright (C) 2026 satromi
#  This software is distributed under the T-License 2.2.
#
#  mkjobs.py: the pictures the board's check of the HEVC decoder decodes
#  (HEVCTEST=1, kernel/sysdepend/rpi5/hevctest.c), as jobs of the device
#  object "動画の復号器" (include/ts/hevc.h), with the hash each decoded
#  picture is to have.
#
#  A few small streams are encoded with ffmpeg's libx265; ffmpeg's
#  bitstream filter trace_headers says what is in their parameter sets
#  and slice headers, and this turns that into T_HEVCJOB, T_HEVCSLICE and
#  the slices' bytes, keeping the slots of the reference pictures the way
#  a program would. ffmpeg decodes the same streams to NV12, or to P010
#  (p010le) for the streams of 10 bits (Main 10); the FNV-1a hash of each
#  picture is what the board has to get. The decoder's record 2 gives the
#  same layouts, so the hash is taken over the picture as it is read.
#
#  ffmpeg's libx265 has to take yuv420p10le for the streams of 10 bits
#  (ffmpeg -h encoder=libx265 lists the pixel formats); the builds of
#  ffmpeg with x265 of several bit depths do.
#
#    python3 mkjobs.py [out.bin]      (ffmpeg on the PATH, or FFMPEG=<its path>)
#
#  The file:
#    "HVTEST02", u32 streams, u32 sizeof(T_HEVCJOB), u32 sizeof(T_HEVCSLICE), u32 0
#    a stream: char name[16], u32 width, height, slots, pictures, bit depth, 0
#    a picture: u32 bytes of the job, u32 the hash, u32 slot, u32 0, the job, to 8 bytes
#

import os
import re
import struct
import subprocess
import sys
import tempfile

# (name, width, height, pictures, x265's parameters, bit depth)
# m10wide is 208 wide: its luma rows and its chroma rows take three of the
# decoder's columns of 96 samples, the last one partly.
STREAMS = [
    ("tiny64", 64, 64, 3, "bframes=0:ref=1:ctu=32:no-wpp=1", 8),
    ("qvga2sl", 320, 240, 4, "bframes=0:ref=1:ctu=64:slices=2", 8),
    ("ctb16", 128, 96, 3, "bframes=0:ref=1:ctu=16:no-wpp=1:no-sao=1", 8),
    ("m10tiny", 64, 64, 3, "bframes=0:ref=1:ctu=32:no-wpp=1", 10),
    ("m10wide", 208, 120, 4, "bframes=0:ref=1:ctu=64:slices=2", 10),
]
NSLOTS = 4

JOB_MAGIC = 0x48455631
MAX_DPB = 16

SPS_SEPARATE_COLOUR_PLANE = 0x0001
SPS_SCALING_LIST = 0x0002
SPS_AMP = 0x0004
SPS_SAO = 0x0008
SPS_PCM = 0x0010
SPS_PCM_LOOP_FILTER_OFF = 0x0020
SPS_TEMPORAL_MVP = 0x0040
SPS_STRONG_INTRA_SMOOTHING = 0x0080

PPS_SIGN_DATA_HIDING = 0x0001
PPS_CONSTRAINED_INTRA = 0x0002
PPS_TRANSFORM_SKIP = 0x0004
PPS_CU_QP_DELTA = 0x0008
PPS_WEIGHTED_PRED = 0x0010
PPS_WEIGHTED_BIPRED = 0x0020
PPS_TRANSQUANT_BYPASS = 0x0040
PPS_TILES = 0x0080
PPS_ENTROPY_SYNC = 0x0100
PPS_LOOP_FILTER_TILES = 0x0200

SL_SAO_LUMA = 0x0001
SL_SAO_CHROMA = 0x0002
SL_TEMPORAL_MVP = 0x0004
SL_MVD_L1_ZERO = 0x0008
SL_CABAC_INIT = 0x0010
SL_COLLOCATED_FROM_L0 = 0x0020
SL_DEBLOCKING_OFF = 0x0040
SL_LOOP_FILTER_SLICES = 0x0080
SL_DEPENDENT = 0x0100

SLICE_B, SLICE_P, SLICE_I = 0, 1, 2

# the C structures' layouts (little-endian, the natural alignment)
F_DPB = "<BBHi"
F_SPS = "<HH14BxxI"
F_PPS = "<bBbbBB20B22BBBxxI"
F_SCALE_BYTES = 6 * 16 + 6 * 64 + 6 * 64 + 2 * 64 + 6 + 2
F_JOB_HEAD = "<6I"
F_SLICE = "<4I7B5b16B16BBb32b32b64b64bHiI"
SIZE_JOB = struct.calcsize(F_JOB_HEAD) + MAX_DPB * struct.calcsize(F_DPB) + struct.calcsize(F_SPS) \
    + struct.calcsize(F_PPS) + F_SCALE_BYTES
SIZE_SLICE = struct.calcsize(F_SLICE)


FFMPEG = os.environ.get("FFMPEG", "ffmpeg")


def run(cmd):
    if cmd[0] == "ffmpeg":
        cmd = [FFMPEG] + cmd[1:]
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if r.returncode != 0:
        sys.exit("%s: %s" % (cmd[0], r.stderr.decode(errors="replace")[-2000:]))
    return r


def fnv1a(b):
    h = 0x811C9DC5
    for x in b:
        h = ((h ^ x) * 0x01000193) & 0xFFFFFFFF
    return h


def nal_units(data):
    """The NAL units of an Annex B stream, without their start codes."""
    out, i, n = [], 0, len(data)
    starts = []
    while i + 3 <= n:
        if data[i] == 0 and data[i + 1] == 0 and data[i + 2] == 1:
            starts.append(i + 3)
            i += 3
        else:
            i += 1
    for k, s in enumerate(starts):
        e = starts[k + 1] - 3 if k + 1 < len(starts) else n
        while e > s and data[e - 1] == 0:		# a zero_byte of the next start code
            e -= 1
        out.append(data[s:e])
    return out


LINE = re.compile(r"\[trace_headers @ [0-9a-fA-Fx]+\] (\d+)\s+(\S+)\s+[01]+ = (-?\d+)\s*$")
TITLE = re.compile(r"\[trace_headers @ [0-9a-fA-Fx]+\] ([A-Za-z][A-Za-z ]+[A-Za-z])\s*$")


def sections(text):
    """trace_headers' output as (title, {name: value}, the bit after the last)."""
    out, cur = [], None
    for ln in text.splitlines():
        m = LINE.search(ln)
        if m and cur is not None:
            pos, name, val = int(m.group(1)), m.group(2), int(m.group(3))
            cur[1][name] = val
            cur[2] = pos + 1
            continue
        t = TITLE.search(ln)
        if t:
            cur = [t.group(1), {}, 0]
            out.append(cur)
    return [tuple(c) for c in out]


def g(d, name, default=0):
    return d.get(name, default)


def rbsp_to_raw(nal, nbytes):
    """Bytes of the NAL unit as it is in the stream that hold its first nbytes without emulation prevention."""
    zeros, got, i = 0, 0, 0
    while got < nbytes:
        b = nal[i]
        if zeros >= 2 and b == 3:
            zeros = 0
            i += 1
            continue
        zeros = zeros + 1 if b == 0 else 0
        got += 1
        i += 1
    return i


class Stream:
    def __init__(self, name, w, h, frames, params, depth, tmp):
        self.name, self.w, self.h, self.depth = name, w, h, depth
        es = os.path.join(tmp, name + ".hevc")
        yuv = os.path.join(tmp, name + ".yuv")
        run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
             "testsrc2=size=%dx%d:rate=25" % (w, h), "-frames:v", str(frames), "-c:v", "libx265",
             "-pix_fmt", "yuv420p10le" if depth == 10 else "yuv420p", "-x265-params", params + ":log-level=error", "-y", es])
        r = run(["ffmpeg", "-hide_banner", "-loglevel", "trace", "-i", es, "-c:v", "copy",
                 "-bsf:v", "trace_headers", "-f", "null", "-"])
        self.secs = sections(r.stderr.decode(errors="replace"))
        run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-i", es, "-pix_fmt",
             "p010le" if depth == 10 else "nv12", "-f", "rawvideo", "-y", yuv])
        raw = open(yuv, "rb").read()
        fs = w * h * 3 // 2 * (2 if depth == 10 else 1)
        self.hashes = [fnv1a(raw[i:i + fs]) for i in range(0, len(raw), fs)]
        self.nals = [n for n in nal_units(open(es, "rb").read()) if ((n[0] >> 1) & 63) < 32]
        os.remove(es)
        os.remove(yuv)

    def jobs(self):
        sps = pps = None
        slices = [s for s in self.secs if s[0] == "Slice Segment Header"]
        for t, d, _ in self.secs:
            if t == "Sequence Parameter Set" and sps is None:
                sps = d
            if t == "Picture Parameter Set" and pps is None:
                pps = d
        if len(slices) != len(self.nals):
            sys.exit("%s: %d slice headers, %d slices" % (self.name, len(slices), len(self.nals)))
        if g(sps, "bit_depth_luma_minus8") + 8 != self.depth or g(sps, "bit_depth_chroma_minus8") + 8 != self.depth:
            sys.exit("%s: %d bits asked, the stream has others" % (self.name, self.depth))
        if g(sps, "scaling_list_enabled_flag") or g(sps, "long_term_ref_pics_present_flag") \
                or g(sps, "num_short_term_ref_pic_sets") or g(pps, "lists_modification_present_flag"):
            sys.exit("%s: a tool the check does not take" % self.name)
        log2_ctb = g(sps, "log2_min_luma_coding_block_size_minus3") + 3 \
            + g(sps, "log2_diff_max_min_luma_coding_block_size")
        cw = (self.w + (1 << log2_ctb) - 1) >> log2_ctb
        max_lsb = 1 << (g(sps, "log2_max_pic_order_cnt_lsb_minus4") + 4)

        # the pictures: their slices together
        pics = []
        for (t, d, end), nal in zip(slices, self.nals):
            if g(d, "first_slice_segment_in_pic_flag"):
                pics.append([])
            pics[-1].append((d, end, nal))

        slot_of = {}			# PicOrderCntVal -> slot
        prev_tid0 = 0
        out = []
        for n, pic in enumerate(pics):
            d0 = pic[0][0]
            nut = g(d0, "nal_unit_type")
            if nut in (19, 20):
                poc = 0
            else:
                lsb = g(d0, "slice_pic_order_cnt_lsb")
                plsb, pmsb = prev_tid0 & (max_lsb - 1), prev_tid0 - (prev_tid0 & (max_lsb - 1))
                if lsb < plsb and plsb - lsb >= max_lsb // 2:
                    msb = pmsb + max_lsb
                elif lsb > plsb and lsb - plsb > max_lsb // 2:
                    msb = pmsb - max_lsb
                else:
                    msb = pmsb
                poc = msb + lsb
            if g(d0, "nuh_temporal_id_plus1") == 1 and not (nut <= 14 and nut % 2 == 0) and nut not in range(6, 10):
                prev_tid0 = poc

            # the short-term reference picture set, given in the slice header
            before, after, foll = [], [], []
            if nut not in (19, 20):
                if g(d0, "short_term_ref_pic_set_sps_flag") or g(d0, "inter_ref_pic_set_prediction_flag"):
                    sys.exit("%s: a reference picture set the check does not take" % self.name)
                p = 0
                for i in range(g(d0, "num_negative_pics")):
                    p -= g(d0, "delta_poc_s0_minus1[%d]" % i) + 1
                    (before if g(d0, "used_by_curr_pic_s0_flag[%d]" % i) else foll).append(poc + p)
                p = 0
                for i in range(g(d0, "num_positive_pics")):
                    p += g(d0, "delta_poc_s1_minus1[%d]" % i) + 1
                    (after if g(d0, "used_by_curr_pic_s1_flag[%d]" % i) else foll).append(poc + p)
            keep = set(before + after + foll)
            slot_of = {k: v for k, v in slot_of.items() if k in keep}
            free = [s for s in range(NSLOTS) if s not in slot_of.values()]
            out_slot = free[0]

            dpb = sorted(set(before + after), key=lambda x: (x not in before, abs(x - poc)))
            dpb_idx = {p: i for i, p in enumerate(dpb)}
            curr = before + after

            sl_bin, bytes_ = b"", b""
            inherit = None
            for d, end, nal in pic:
                if g(d, "dependent_slice_segment_flag"):
                    dd = dict(inherit)
                    dd.update({k: v for k, v in d.items() if k in ("slice_segment_address",)})
                    dd["dependent_slice_segment_flag"] = 1
                    d = dd
                else:
                    inherit = d
                st = g(d, "slice_type")
                if st == SLICE_I:
                    nl0 = nl1 = 0
                else:
                    if g(d, "num_ref_idx_active_override_flag"):
                        nl0 = g(d, "num_ref_idx_l0_active_minus1") + 1
                        nl1 = g(d, "num_ref_idx_l1_active_minus1") + 1
                    else:
                        nl0 = g(pps, "num_ref_idx_l0_default_active_minus1") + 1
                        nl1 = g(pps, "num_ref_idx_l1_default_active_minus1") + 1
                    if st == SLICE_P:
                        nl1 = 0
                ntemp0 = max(nl0, len(curr))
                l0t = (before + after) * (ntemp0 // max(1, len(curr)) + 1)
                l1t = (after + before) * (ntemp0 // max(1, len(curr)) + 1)
                ref_l0 = [dpb_idx[x] for x in l0t[:nl0]] + [0] * (MAX_DPB - nl0)
                ref_l1 = [dpb_idx[x] for x in l1t[:nl1]] + [0] * (MAX_DPB - nl1)

                # the weights
                denom = g(d, "luma_log2_weight_denom")
                cdenom = denom + g(d, "delta_chroma_log2_weight_denom")
                dlw, lo, dcw, co = [[0] * 32, [0] * 32, [0] * 64, [0] * 64]
                for l, nl in ((0, nl0), (1, nl1)):
                    for i in range(nl):
                        if g(d, "luma_weight_l%d_flag[%d]" % (l, i)):
                            dlw[l * 16 + i] = g(d, "delta_luma_weight_l%d[%d]" % (l, i))
                            lo[l * 16 + i] = g(d, "luma_offset_l%d[%d]" % (l, i))
                        if g(d, "chroma_weight_l%d_flag[%d]" % (l, i)):
                            for j in range(2):
                                dw = g(d, "delta_chroma_weight_l%d[%d][%d]" % (l, i, j))
                                do = g(d, "delta_chroma_offset_l%d[%d][%d]" % (l, i, j))
                                w = (1 << cdenom) + dw
                                dcw[(l * 16 + i) * 2 + j] = dw
                                co[(l * 16 + i) * 2 + j] = max(-128, min(127, 128 + do - ((128 * w) >> cdenom)))

                fl = 0
                if g(d, "slice_sao_luma_flag"):
                    fl |= SL_SAO_LUMA
                if g(d, "slice_sao_chroma_flag"):
                    fl |= SL_SAO_CHROMA
                if g(d, "slice_temporal_mvp_enabled_flag"):
                    fl |= SL_TEMPORAL_MVP
                if g(d, "mvd_l1_zero_flag"):
                    fl |= SL_MVD_L1_ZERO
                if g(d, "cabac_init_flag"):
                    fl |= SL_CABAC_INIT
                if g(d, "collocated_from_l0_flag", 1):
                    fl |= SL_COLLOCATED_FROM_L0
                if g(d, "slice_deblocking_filter_disabled_flag", g(pps, "pps_deblocking_filter_disabled_flag")):
                    fl |= SL_DEBLOCKING_OFF
                if g(d, "slice_loop_filter_across_slices_enabled_flag",
                     g(pps, "pps_loop_filter_across_slices_enabled_flag")):
                    fl |= SL_LOOP_FILTER_SLICES
                if g(d, "dependent_slice_segment_flag"):
                    fl |= SL_DEPENDENT
                if end % 8:
                    sys.exit("%s: a slice header not ending on a byte" % self.name)
                beta = g(d, "slice_beta_offset_div2", g(pps, "pps_beta_offset_div2"))
                tc = g(d, "slice_tc_offset_div2", g(pps, "pps_tc_offset_div2"))
                sl_bin += struct.pack(
                    F_SLICE, len(bytes_), len(nal), rbsp_to_raw(nal, end // 8), g(d, "slice_segment_address"),
                    st, nut, g(d, "nuh_temporal_id_plus1"), max(nl0 - 1, 0), max(nl1 - 1, 0),
                    g(d, "collocated_ref_idx"), g(d, "five_minus_max_num_merge_cand"),
                    g(d, "slice_qp_delta"), g(d, "slice_cb_qp_offset"), g(d, "slice_cr_qp_offset"), beta, tc,
                    *ref_l0, *ref_l1, denom, g(d, "delta_chroma_log2_weight_denom"),
                    *dlw, *lo, *dcw, *co, 0, poc, fl)
                bytes_ += nal

            sflags = 0
            for k, f in (("separate_colour_plane_flag", SPS_SEPARATE_COLOUR_PLANE),
                         ("scaling_list_enabled_flag", SPS_SCALING_LIST), ("amp_enabled_flag", SPS_AMP),
                         ("sample_adaptive_offset_enabled_flag", SPS_SAO), ("pcm_enabled_flag", SPS_PCM),
                         ("pcm_loop_filter_disabled_flag", SPS_PCM_LOOP_FILTER_OFF),
                         ("sps_temporal_mvp_enabled_flag", SPS_TEMPORAL_MVP),
                         ("strong_intra_smoothing_enabled_flag", SPS_STRONG_INTRA_SMOOTHING)):
                if g(sps, k):
                    sflags |= f
            pcm = g(sps, "pcm_enabled_flag")
            sps_bin = struct.pack(
                F_SPS, g(sps, "pic_width_in_luma_samples"), g(sps, "pic_height_in_luma_samples"),
                g(sps, "bit_depth_luma_minus8") + 8, g(sps, "bit_depth_chroma_minus8") + 8,
                g(sps, "chroma_format_idc"), g(sps, "log2_min_luma_coding_block_size_minus3") + 3,
                g(sps, "log2_diff_max_min_luma_coding_block_size"),
                g(sps, "log2_min_luma_transform_block_size_minus2") + 2,
                g(sps, "log2_diff_max_min_luma_transform_block_size"),
                g(sps, "max_transform_hierarchy_depth_inter"), g(sps, "max_transform_hierarchy_depth_intra"),
                g(sps, "pcm_sample_bit_depth_luma_minus1") + 1 if pcm else 0,
                g(sps, "pcm_sample_bit_depth_chroma_minus1") + 1 if pcm else 0,
                g(sps, "log2_min_pcm_luma_coding_block_size_minus3") + 3 if pcm else 0,
                g(sps, "log2_diff_max_min_pcm_luma_coding_block_size"),
                g(sps, "sps_max_sub_layers_minus1"), sflags)

            pflags = 0
            for k, f in (("sign_data_hiding_enabled_flag", PPS_SIGN_DATA_HIDING),
                         ("constrained_intra_pred_flag", PPS_CONSTRAINED_INTRA),
                         ("transform_skip_enabled_flag", PPS_TRANSFORM_SKIP),
                         ("cu_qp_delta_enabled_flag", PPS_CU_QP_DELTA), ("weighted_pred_flag", PPS_WEIGHTED_PRED),
                         ("weighted_bipred_flag", PPS_WEIGHTED_BIPRED),
                         ("transquant_bypass_enabled_flag", PPS_TRANSQUANT_BYPASS),
                         ("tiles_enabled_flag", PPS_TILES), ("entropy_coding_sync_enabled_flag", PPS_ENTROPY_SYNC),
                         ("loop_filter_across_tiles_enabled_flag", PPS_LOOP_FILTER_TILES)):
                if g(pps, k):
                    pflags |= f
            ncol, nrow = g(pps, "num_tile_columns_minus1") + 1, g(pps, "num_tile_rows_minus1") + 1
            ch = (self.h + (1 << log2_ctb) - 1) >> log2_ctb
            if g(pps, "tiles_enabled_flag") and g(pps, "uniform_spacing_flag"):
                cwm = [((i + 1) * cw) // ncol - (i * cw) // ncol - 1 for i in range(ncol)]
                rhm = [((i + 1) * ch) // nrow - (i * ch) // nrow - 1 for i in range(nrow)]
            else:
                cwm = [g(pps, "column_width_minus1[%d]" % i) for i in range(ncol)]
                rhm = [g(pps, "row_height_minus1[%d]" % i) for i in range(nrow)]
            cwm = (cwm + [0] * 20)[:20]
            rhm = (rhm + [0] * 22)[:22]
            pps_bin = struct.pack(
                F_PPS, g(pps, "init_qp_minus26"), g(pps, "diff_cu_qp_delta_depth"), g(pps, "pps_cb_qp_offset"),
                g(pps, "pps_cr_qp_offset"), ncol - 1, nrow - 1, *cwm, *rhm,
                g(pps, "log2_parallel_merge_level_minus2"), 0, pflags)

            dpb_bin = b"".join(struct.pack(F_DPB, slot_of[p], 0, 0, p) for p in dpb)
            dpb_bin += struct.pack(F_DPB, 0, 0, 0, 0) * (MAX_DPB - len(dpb))
            size = SIZE_JOB + len(pic) * SIZE_SLICE + len(bytes_)
            job = struct.pack(F_JOB_HEAD, JOB_MAGIC, size, out_slot, len(pic), len(dpb), 0) + dpb_bin \
                + sps_bin + pps_bin + bytes(F_SCALE_BYTES) + sl_bin + bytes_
            assert len(job) == size
            out.append((job, self.hashes[n], out_slot))
            slot_of[poc] = out_slot
        return out


def main():
    dst = sys.argv[1] if len(sys.argv) > 1 else "hevc_jobs.bin"
    with tempfile.TemporaryDirectory() as tmp:
        body = b""
        for name, w, h, n, params, depth in STREAMS:
            s = Stream(name, w, h, n, params, depth, tmp)
            jobs = s.jobs()
            if len(jobs) != len(s.hashes):
                sys.exit("%s: %d jobs, %d pictures decoded" % (name, len(jobs), len(s.hashes)))
            body += name.encode().ljust(16, b"\0") + struct.pack("<6I", w, h, NSLOTS, len(jobs), depth, 0)
            for job, hsh, slot in jobs:
                body += struct.pack("<4I", len(job), hsh, slot, 0) + job + bytes(-len(job) % 8)
            print("%s: %dx%d, %d bits, %d pictures, %s" % (name, w, h, depth, len(jobs),
                                                         " ".join("%08x" % j[1] for j in jobs)))
    head = b"HVTEST02" + struct.pack("<4I", len(STREAMS), SIZE_JOB, SIZE_SLICE, 0)
    with open(dst, "wb") as f:
        f.write(head + body)
    print("%s: %d bytes (T_HEVCJOB %d, T_HEVCSLICE %d)" % (dst, len(head) + len(body), SIZE_JOB, SIZE_SLICE))


if __name__ == "__main__":
    main()
