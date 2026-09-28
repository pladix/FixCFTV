/**
 * FixCFTV - CCTV & Security Video Restoration Engine
 * Desenvolvido por PladixOficial (https://github.com/pladix/FixCFTV)
 */

#include "healer.h"
#include "nal_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>

static int is_avcc_stream(const uint8_t *buf, size_t size) {
    if (size < 8) return 0;
    size_t sc_len = 0;
    if (nal_find_start_code(buf, size < 32 ? size : 32, &sc_len) != NULL) {
        return 0;
    }
    uint32_t len = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | buf[3];
    if (len > 0 && len < 10 * 1024 * 1024 && (len + 4) <= size) {
        uint8_t nal_type = buf[4] & 0x1F;
        if ((buf[4] & 0x80) == 0 && nal_type >= 1 && nal_type <= 12) {
            return 1;
        }
    }
    return 0;
}

static int extract_sps_pps_from_extradata(enum AVCodecID codec_id, const uint8_t *extradata, int extradata_size,
                                         uint8_t **out_annexb, int *out_annexb_size) {
    if (!extradata || extradata_size < 7) return -1;

    if (codec_id == AV_CODEC_ID_H264) {
        if (extradata[0] != 1) {
            *out_annexb = (uint8_t *)malloc(extradata_size);
            if (!*out_annexb) return -1;
            memcpy(*out_annexb, extradata, extradata_size);
            *out_annexb_size = extradata_size;
            return 0;
        }

        int sps_count = extradata[5] & 0x1F;
        const uint8_t *p = extradata + 6;
        const uint8_t *end = extradata + extradata_size;

        uint8_t *annexb = (uint8_t *)malloc(extradata_size + 64);
        if (!annexb) return -1;
        int pos = 0;

        for (int i = 0; i < sps_count; i++) {
            if (p + 2 > end) break;
            int sps_len = ((int)p[0] << 8) | p[1];
            p += 2;
            if (p + sps_len > end) break;

            annexb[pos++] = 0x00;
            annexb[pos++] = 0x00;
            annexb[pos++] = 0x00;
            annexb[pos++] = 0x01;
            memcpy(annexb + pos, p, sps_len);
            pos += sps_len;
            p += sps_len;
        }

        if (p < end) {
            int pps_count = *p++;
            for (int i = 0; i < pps_count; i++) {
                if (p + 2 > end) break;
                int pps_len = ((int)p[0] << 8) | p[1];
                p += 2;
                if (p + pps_len > end) break;

                annexb[pos++] = 0x00;
                annexb[pos++] = 0x00;
                annexb[pos++] = 0x00;
                annexb[pos++] = 0x01;
                memcpy(annexb + pos, p, pps_len);
                pos += pps_len;
                p += pps_len;
            }
        }

        *out_annexb = annexb;
        *out_annexb_size = pos;
        return 0;
    }

    return -1;
}

// -----------------------------------------------------------------------------
// Método 1: Restauração Permissiva de Contêiner & Re-indexação FastStart
// -----------------------------------------------------------------------------
int healer_repair_permissive_container(const HealerConfig *config, HealerStats *stats) {
    stats->repair_mode_name = "Restauração Permissiva de Contêiner & FastStart (Método 1)";
    stats->start_time = utils_get_time_sec();

    int64_t file_size = utils_get_file_size(config->input_path);
    if (file_size <= 0) return -1;
    stats->total_bytes = file_size;

    AVDictionary *in_opts = NULL;
    av_dict_set(&in_opts, "probesize", "100000000", 0);
    av_dict_set(&in_opts, "analyzeduration", "100000000", 0);
    av_dict_set(&in_opts, "err_detect", "ignore_err", 0);
    av_dict_set(&in_opts, "fflags", "+genpts+discardcorrupt+nobuffer", 0);

    AVFormatContext *in_fmt = NULL;
    int ret = avformat_open_input(&in_fmt, config->input_path, NULL, &in_opts);
    av_dict_free(&in_opts);
    if (ret < 0 || !in_fmt) {
        return -1;
    }

    ret = avformat_find_stream_info(in_fmt, NULL);
    if (ret < 0) {
        avformat_close_input(&in_fmt);
        return -1;
    }

    int v_idx = av_find_best_stream(in_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (v_idx < 0) {
        avformat_close_input(&in_fmt);
        return -1;
    }

    AVStream *in_stream = in_fmt->streams[v_idx];
    stats->codec_id = in_stream->codecpar->codec_id;
    stats->width = in_stream->codecpar->width;
    stats->height = in_stream->codecpar->height;

    int fps = config->default_fps > 0 ? config->default_fps : 25;
    if (in_stream->avg_frame_rate.num > 0 && in_stream->avg_frame_rate.den > 0) {
        fps = in_stream->avg_frame_rate.num / in_stream->avg_frame_rate.den;
        stats->framerate = in_stream->avg_frame_rate;
    } else {
        stats->framerate = (AVRational){fps, 1};
    }
    if (fps <= 0) fps = 25;

    AVFormatContext *out_fmt = NULL;
    ret = avformat_alloc_output_context2(&out_fmt, NULL, "mp4", config->output_path);
    if (ret < 0 || !out_fmt) {
        avformat_close_input(&in_fmt);
        return -1;
    }

    AVStream *out_stream = avformat_new_stream(out_fmt, NULL);
    if (!out_stream) {
        avformat_free_context(out_fmt);
        avformat_close_input(&in_fmt);
        return -1;
    }

    ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
    if (ret < 0) {
        avformat_free_context(out_fmt);
        avformat_close_input(&in_fmt);
        return -1;
    }
    out_stream->codecpar->codec_tag = 0;
    out_stream->time_base = (AVRational){1, 90000};

    AVDictionary *out_opts = NULL;
    av_dict_set(&out_opts, "movflags", "faststart", 0);

    ret = avio_open(&out_fmt->pb, config->output_path, AVIO_FLAG_WRITE);
    if (ret < 0) {
        av_dict_free(&out_opts);
        avformat_free_context(out_fmt);
        avformat_close_input(&in_fmt);
        return -1;
    }

    ret = avformat_write_header(out_fmt, &out_opts);
    av_dict_free(&out_opts);
    if (ret < 0) {
        avio_closep(&out_fmt->pb);
        avformat_free_context(out_fmt);
        avformat_close_input(&in_fmt);
        return -1;
    }

    AVPacket *pkt = av_packet_alloc();
    int64_t frame_count = 0;
    int64_t keyframe_count = 0;
    int64_t pts_step = 90000 / fps;

    while (av_read_frame(in_fmt, pkt) >= 0) {
        if (pkt->stream_index == v_idx) {
            pkt->stream_index = out_stream->index;
            pkt->pts = frame_count * pts_step;
            pkt->dts = pkt->pts;
            pkt->duration = pts_step;

            if (pkt->flags & AV_PKT_FLAG_KEY) keyframe_count++;

            av_interleaved_write_frame(out_fmt, pkt);
            frame_count++;

            if (frame_count % 30 == 0) {
                double elapsed = utils_get_time_sec() - stats->start_time;
                utils_print_progress(frame_count * 20000, file_size, frame_count, elapsed);
            }
        }
        av_packet_unref(pkt);
    }

    av_write_trailer(out_fmt);

    stats->end_time = utils_get_time_sec();
    stats->processed_bytes = file_size;
    stats->recovered_frames = frame_count;
    stats->keyframes = keyframe_count;

    av_packet_free(&pkt);
    avio_closep(&out_fmt->pb);
    avformat_free_context(out_fmt);
    avformat_close_input(&in_fmt);

    return (frame_count > 0) ? 0 : -1;
}

// -----------------------------------------------------------------------------
// Método 2: Varredura Bruta de NAL Units & Remux FastStart (Annex B)
// -----------------------------------------------------------------------------
int healer_repair_raw_scan(const HealerConfig *config, HealerStats *stats) {
    stats->repair_mode_name = "Varredura Bruta de NAL Units & Remux FastStart (Método 2)";
    stats->start_time = utils_get_time_sec();

    int64_t file_size = utils_get_file_size(config->input_path);
    if (file_size <= 0) {
        fprintf(stderr, "[-] Erro: O arquivo corrompido '%s' está vazio ou inacessível.\n", config->input_path);
        return -1;
    }
    stats->total_bytes = file_size;

    FILE *fp = fopen(config->input_path, "rb");
    if (!fp) {
        fprintf(stderr, "[-] Erro: Não foi possível abrir o arquivo de entrada '%s'.\n", config->input_path);
        return -1;
    }

    int64_t data_offset = 0;
    int64_t data_size = file_size;
    nal_locate_mdat(fp, file_size, &data_offset, &data_size);

    if (config->verbose) {
        fprintf(stdout, "[+] Tamanho do arquivo: %lld bytes, payload detectado no offset: %lld\n",
                (long long)file_size, (long long)data_offset);
    }

#ifdef _WIN32
    _fseeki64(fp, data_offset, SEEK_SET);
#else
    fseeko(fp, data_offset, SEEK_SET);
#endif

    size_t probe_len = 2 * 1024 * 1024;
    if ((int64_t)probe_len > data_size) probe_len = (size_t)data_size;

    uint8_t *probe_buf = (uint8_t *)malloc(probe_len + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!probe_buf) {
        fclose(fp);
        return -1;
    }
    memset(probe_buf + probe_len, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    size_t probe_read = fread(probe_buf, 1, probe_len, fp);
    if (probe_read == 0) {
        free(probe_buf);
        fclose(fp);
        return -1;
    }

    int avcc_mode = is_avcc_stream(probe_buf, probe_read);
    if (avcc_mode) {
        size_t p_idx = 0;
        while (p_idx + 4 < probe_read) {
            uint32_t nal_len = ((uint32_t)probe_buf[p_idx] << 24) |
                               ((uint32_t)probe_buf[p_idx+1] << 16) |
                               ((uint32_t)probe_buf[p_idx+2] << 8) |
                               probe_buf[p_idx+3];
            if (nal_len > 0 && nal_len < 10 * 1024 * 1024 && p_idx + 4 + nal_len <= probe_read) {
                probe_buf[p_idx]   = 0x00;
                probe_buf[p_idx+1] = 0x00;
                probe_buf[p_idx+2] = 0x00;
                probe_buf[p_idx+3] = 0x01;
                p_idx += 4 + nal_len;
            } else {
                p_idx++;
            }
        }
    }

    enum AVCodecID codec_id = nal_probe_codec(probe_buf, probe_read);
    stats->codec_id = codec_id;

    int sps_w = 0, sps_h = 0;
    uint8_t *cached_param_sets = NULL;
    int cached_param_sets_size = 0;

    const uint8_t *p = probe_buf;
    const uint8_t *p_end = probe_buf + probe_read;
    const uint8_t *first_sps_sc = NULL;
    const uint8_t *last_param_end = NULL;

    while (p < p_end) {
        size_t sc_len = 0;
        const uint8_t *sc = nal_find_start_code(p, p_end - p, &sc_len);
        if (!sc) break;
        const uint8_t *nal_data = sc + sc_len;
        if (nal_data >= p_end) break;
        int nal_type = *nal_data & 0x1F;
        if (nal_type == H264_NAL_SPS) {
            if (!first_sps_sc) first_sps_sc = sc;
            size_t next_sc_len = 0;
            const uint8_t *next_sc = nal_find_start_code(nal_data, p_end - nal_data, &next_sc_len);
            size_t sps_size = next_sc ? (size_t)(next_sc - nal_data) : (size_t)(p_end - nal_data);
            if (nal_parse_h264_sps(nal_data, sps_size, &sps_w, &sps_h) == 0) {
                if (config->verbose) {
                    fprintf(stdout, "[+] SPS H.264 decodificado com sucesso: Resolução %dx%d\n", sps_w, sps_h);
                }
            }
            if (next_sc) last_param_end = next_sc;
        } else if (nal_type == H264_NAL_PPS) {
            size_t next_sc_len = 0;
            const uint8_t *next_sc = nal_find_start_code(nal_data, p_end - nal_data, &next_sc_len);
            last_param_end = next_sc ? next_sc : p_end;
        }
        p = sc + sc_len + 1;
    }

    if (first_sps_sc && last_param_end && last_param_end > first_sps_sc) {
        cached_param_sets_size = (int)(last_param_end - first_sps_sc);
        cached_param_sets = (uint8_t *)malloc(cached_param_sets_size);
        if (cached_param_sets) {
            memcpy(cached_param_sets, first_sps_sc, cached_param_sets_size);
        }
    }
    free(probe_buf);

#ifdef _WIN32
    _fseeki64(fp, data_offset, SEEK_SET);
#else
    fseeko(fp, data_offset, SEEK_SET);
#endif

    const AVCodec *decoder = avcodec_find_decoder(codec_id);
    if (!decoder) {
        fclose(fp);
        return -1;
    }

    AVCodecParserContext *parser = av_parser_init(codec_id);
    if (!parser) {
        fclose(fp);
        return -1;
    }

    AVCodecContext *codec_ctx = avcodec_alloc_context3(decoder);
    if (!codec_ctx) {
        av_parser_close(parser);
        fclose(fp);
        return -1;
    }

    AVPacket *pkt = av_packet_alloc();
    if (!pkt) {
        avcodec_free_context(&codec_ctx);
        av_parser_close(parser);
        fclose(fp);
        return -1;
    }

    AVFormatContext *out_fmt_ctx = NULL;
    AVStream *out_vstream = NULL;
    int muxer_initialized = 0;

    int fps = config->default_fps > 0 ? config->default_fps : 25;
    stats->framerate = (AVRational){fps, 1};

    uint8_t *chunk_buf = (uint8_t *)malloc(CHUNK_BUFFER_SIZE + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!chunk_buf) {
        av_packet_free(&pkt);
        avcodec_free_context(&codec_ctx);
        av_parser_close(parser);
        fclose(fp);
        return -1;
    }

    int64_t current_processed = data_offset;
    int64_t frame_count = 0;
    int64_t keyframe_count = 0;

    while (!feof(fp)) {
        size_t bytes_read = fread(chunk_buf, 1, CHUNK_BUFFER_SIZE, fp);
        if (bytes_read == 0) break;

        memset(chunk_buf + bytes_read, 0, AV_INPUT_BUFFER_PADDING_SIZE);

        if (avcc_mode) {
            size_t idx = 0;
            while (idx + 4 < bytes_read) {
                uint32_t nal_len = ((uint32_t)chunk_buf[idx] << 24) |
                                   ((uint32_t)chunk_buf[idx+1] << 16) |
                                   ((uint32_t)chunk_buf[idx+2] << 8) |
                                   chunk_buf[idx+3];
                int is_valid = 0;
                if (nal_len >= 4 && nal_len <= 500000 && idx + 4 + nal_len <= bytes_read) {
                    uint8_t b1 = chunk_buf[idx + 4];
                    if ((b1 & 0x80) == 0) {
                        uint8_t t = b1 & 0x1F;
                        if ((t == H264_NAL_SPS && nal_len <= 128) ||
                            (t == H264_NAL_PPS && nal_len <= 64) ||
                            (t == H264_NAL_IDR_SLICE && nal_len >= 20 && (chunk_buf[idx + 5] & 0x80) != 0) ||
                            (t == H264_NAL_SLICE && nal_len >= 10 && (chunk_buf[idx + 5] & 0x80) != 0) ||
                            ((t == H264_NAL_SEI || t == H264_NAL_AUD) && nal_len <= 2048)) {
                            is_valid = 1;
                        }
                    }
                }
                if (is_valid) {
                    chunk_buf[idx]   = 0x00;
                    chunk_buf[idx+1] = 0x00;
                    chunk_buf[idx+2] = 0x00;
                    chunk_buf[idx+3] = 0x01;
                    idx += 4 + nal_len;
                } else {
                    idx++;
                }
            }
        }

        uint8_t *cur_ptr = chunk_buf;
        int cur_size = (int)bytes_read;

        while (cur_size > 0) {
            uint8_t *pout_data = NULL;
            int pout_size = 0;

            int len = av_parser_parse2(parser, codec_ctx,
                                       &pout_data, &pout_size,
                                       cur_ptr, cur_size,
                                       AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
            if (len < 0) {
                cur_ptr++;
                cur_size--;
                stats->dropped_bytes++;
                continue;
            }

            cur_ptr += len;
            cur_size -= len;

            if (pout_size > 0 && pout_data != NULL) {
                if (!muxer_initialized) {
                    int w = parser->width > 0 ? parser->width : sps_w;
                    int h = parser->height > 0 ? parser->height : sps_h;
                    if (w <= 0 || h <= 0) {
                        w = 1920;
                        h = 1080;
                    }
                    stats->width = w;
                    stats->height = h;

                    int ret = avformat_alloc_output_context2(&out_fmt_ctx, NULL, "mp4", config->output_path);
                    if (ret < 0 || !out_fmt_ctx) goto cleanup;

                    out_vstream = avformat_new_stream(out_fmt_ctx, NULL);
                    if (!out_vstream) goto cleanup;

                    out_vstream->codecpar->codec_id = codec_id;
                    out_vstream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
                    out_vstream->codecpar->width = w;
                    out_vstream->codecpar->height = h;
                    out_vstream->codecpar->format = AV_PIX_FMT_YUV420P;
                    out_vstream->codecpar->codec_tag = 0;
                    out_vstream->time_base = (AVRational){1, 90000};

                    AVDictionary *opts = NULL;
                    av_dict_set(&opts, "movflags", "faststart", 0);

                    ret = avio_open(&out_fmt_ctx->pb, config->output_path, AVIO_FLAG_WRITE);
                    if (ret < 0) {
                        av_dict_free(&opts);
                        goto cleanup;
                    }

                    ret = avformat_write_header(out_fmt_ctx, &opts);
                    av_dict_free(&opts);
                    if (ret < 0) goto cleanup;

                    muxer_initialized = 1;
                }

                av_packet_unref(pkt);
                int pkt_alloc_ok = 0;
                if (frame_count == 0 && cached_param_sets && cached_param_sets_size > 0) {
                    if (av_new_packet(pkt, cached_param_sets_size + pout_size) == 0) {
                        memcpy(pkt->data, cached_param_sets, cached_param_sets_size);
                        memcpy(pkt->data + cached_param_sets_size, pout_data, pout_size);
                        pkt_alloc_ok = 1;
                    }
                } else {
                    if (av_new_packet(pkt, pout_size) == 0) {
                        memcpy(pkt->data, pout_data, pout_size);
                        pkt_alloc_ok = 1;
                    }
                }

                if (pkt_alloc_ok) {
                    int64_t pts_step = 90000 / fps;
                    pkt->pts = frame_count * pts_step;
                    pkt->dts = pkt->pts;
                    pkt->duration = pts_step;
                    pkt->stream_index = out_vstream->index;

                    if (parser->key_frame == 1) {
                        pkt->flags |= AV_PKT_FLAG_KEY;
                        keyframe_count++;
                    }

                    av_interleaved_write_frame(out_fmt_ctx, pkt);
                    frame_count++;
                }
            }
        }

        current_processed += bytes_read;
        double elapsed = utils_get_time_sec() - stats->start_time;
        utils_print_progress(current_processed, file_size, frame_count, elapsed);
    }

    if (muxer_initialized) {
        uint8_t *pout_data = NULL;
        int pout_size = 0;
        av_parser_parse2(parser, codec_ctx, &pout_data, &pout_size, NULL, 0,
                         AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        if (pout_size > 0 && pout_data != NULL) {
            av_packet_unref(pkt);
            if (av_new_packet(pkt, pout_size) == 0) {
                memcpy(pkt->data, pout_data, pout_size);
                int64_t pts_step = 90000 / fps;
                pkt->pts = frame_count * pts_step;
                pkt->dts = pkt->pts;
                pkt->duration = pts_step;
                pkt->stream_index = out_vstream->index;
                if (parser->key_frame == 1) {
                    pkt->flags |= AV_PKT_FLAG_KEY;
                    keyframe_count++;
                }
                av_interleaved_write_frame(out_fmt_ctx, pkt);
                frame_count++;
            }
        }
        av_write_trailer(out_fmt_ctx);
    }

    stats->end_time = utils_get_time_sec();
    stats->processed_bytes = current_processed;
    stats->recovered_frames = frame_count;
    stats->keyframes = keyframe_count;

cleanup:
    if (cached_param_sets) free(cached_param_sets);
    free(chunk_buf);
    av_packet_free(&pkt);
    avcodec_free_context(&codec_ctx);
    av_parser_close(parser);
    if (out_fmt_ctx) {
        if (out_fmt_ctx->pb) {
            avio_closep(&out_fmt_ctx->pb);
        }
        avformat_free_context(out_fmt_ctx);
    }
    fclose(fp);

    return (frame_count > 0) ? 0 : -1;
}

// -----------------------------------------------------------------------------
// Método 3: Reconstrução Cirúrgica de Stream AVCC Intercalado & FastStart
// -----------------------------------------------------------------------------
int healer_repair_avcc(const HealerConfig *config, HealerStats *stats) {
    stats->repair_mode_name = "Reconstrução Cirúrgica de Stream AVCC Intercalado (Método 3)";
    stats->start_time = utils_get_time_sec();

    int64_t file_size = utils_get_file_size(config->input_path);
    if (file_size <= 0) return -1;
    stats->total_bytes = file_size;

    FILE *fp = fopen(config->input_path, "rb");
    if (!fp) return -1;

    int64_t data_offset = 0;
    int64_t data_size = file_size;
    nal_locate_mdat(fp, file_size, &data_offset, &data_size);

#ifdef _WIN32
    _fseeki64(fp, data_offset, SEEK_SET);
#else
    fseeko(fp, data_offset, SEEK_SET);
#endif

    enum AVCodecID codec_id = AV_CODEC_ID_H264;
    stats->codec_id = codec_id;

    const AVCodec *decoder = avcodec_find_decoder(codec_id);
    if (!decoder) {
        fclose(fp);
        return -1;
    }

    AVCodecParserContext *parser = av_parser_init(codec_id);
    if (!parser) {
        fclose(fp);
        return -1;
    }

    AVCodecContext *codec_ctx = avcodec_alloc_context3(decoder);
    if (!codec_ctx) {
        av_parser_close(parser);
        fclose(fp);
        return -1;
    }

    AVPacket *pkt = av_packet_alloc();
    if (!pkt) {
        avcodec_free_context(&codec_ctx);
        av_parser_close(parser);
        fclose(fp);
        return -1;
    }

    AVFormatContext *out_fmt_ctx = NULL;
    AVStream *out_vstream = NULL;
    int muxer_initialized = 0;

    int fps = config->default_fps > 0 ? config->default_fps : 17;
    stats->framerate = (AVRational){fps, 1};

    size_t sliding_buf_size = 4 * 1024 * 1024;
    uint8_t *buf = (uint8_t *)malloc(sliding_buf_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!buf) {
        av_packet_free(&pkt);
        avcodec_free_context(&codec_ctx);
        av_parser_close(parser);
        fclose(fp);
        return -1;
    }

    size_t annexb_buf_size = 1024 * 1024;
    uint8_t *annexb_buf = (uint8_t *)malloc(annexb_buf_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!annexb_buf) {
        free(buf);
        av_packet_free(&pkt);
        avcodec_free_context(&codec_ctx);
        av_parser_close(parser);
        fclose(fp);
        return -1;
    }

    int sps_w = 0, sps_h = 0;
    size_t buf_len = fread(buf, 1, sliding_buf_size, fp);
    memset(buf + buf_len, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    size_t pos = 0;
    int64_t total_bytes_read = buf_len;
    int64_t frame_count = 0;
    int64_t keyframe_count = 0;
    int first_sps_seen = 0;

    while (pos < buf_len || !feof(fp)) {
        if (buf_len - pos < 512 * 1024 && !feof(fp)) {
            size_t unread = buf_len - pos;
            if (unread > 0) {
                memmove(buf, buf + pos, unread);
            }
            pos = 0;
            size_t to_read = sliding_buf_size - unread;
            size_t n = fread(buf + unread, 1, to_read, fp);
            total_bytes_read += n;
            buf_len = unread + n;
            memset(buf + buf_len, 0, AV_INPUT_BUFFER_PADDING_SIZE);
            if (buf_len == 0) break;
        }

        if (pos + 4 > buf_len) break;

        uint32_t nal_len = ((uint32_t)buf[pos] << 24) |
                           ((uint32_t)buf[pos+1] << 16) |
                           ((uint32_t)buf[pos+2] << 8) |
                           buf[pos+3];

        int frame_emitted = 0;

        // 1. Verificação de Quadro-Chave Completo (SPS -> PPS -> IDR)
        if (nal_len >= 15 && nal_len <= 128 && pos + 4 + nal_len + 8 <= buf_len) {
            uint8_t b1 = buf[pos + 4];
            if ((b1 & 0x80) == 0 && (b1 & 0x1F) == H264_NAL_SPS) {
                size_t pps_pos = pos + 4 + nal_len;
                uint32_t pps_len = ((uint32_t)buf[pps_pos] << 24) |
                                   ((uint32_t)buf[pps_pos+1] << 16) |
                                   ((uint32_t)buf[pps_pos+2] << 8) |
                                   buf[pps_pos+3];
                if (pps_len >= 4 && pps_len <= 64 && pps_pos + 4 + pps_len + 8 <= buf_len) {
                    uint8_t pps_b1 = buf[pps_pos + 4];
                    if ((pps_b1 & 0x80) == 0 && (pps_b1 & 0x1F) == H264_NAL_PPS) {
                        size_t idr_pos = pps_pos + 4 + pps_len;
                        uint32_t idr_len = ((uint32_t)buf[idr_pos] << 24) |
                                           ((uint32_t)buf[idr_pos+1] << 16) |
                                           ((uint32_t)buf[idr_pos+2] << 8) |
                                           buf[idr_pos+3];
                        if (idr_len >= 50 && idr_len <= 500000 && idr_pos + 4 + idr_len <= buf_len) {
                            uint8_t idr_b1 = buf[idr_pos + 4];
                            uint8_t idr_b2 = buf[idr_pos + 5];
                            if ((idr_b1 & 0x80) == 0 && (idr_b1 & 0x1F) == H264_NAL_IDR_SLICE && (idr_b2 & 0x80) != 0) {
                                first_sps_seen = 1;
                                if (sps_w <= 0 || sps_h <= 0) {
                                    nal_parse_h264_sps(buf + pos + 4, nal_len, &sps_w, &sps_h);
                                }
                                size_t total_key_size = 4 + nal_len + 4 + pps_len + 4 + idr_len;
                                if (total_key_size + AV_INPUT_BUFFER_PADDING_SIZE > annexb_buf_size) {
                                    annexb_buf_size = total_key_size + 65536;
                                    annexb_buf = (uint8_t *)realloc(annexb_buf, annexb_buf_size + AV_INPUT_BUFFER_PADDING_SIZE);
                                }
                                size_t kpos = 0;
                                annexb_buf[kpos++] = 0x00; annexb_buf[kpos++] = 0x00; annexb_buf[kpos++] = 0x00; annexb_buf[kpos++] = 0x01;
                                memcpy(annexb_buf + kpos, buf + pos + 4, nal_len); kpos += nal_len;
                                annexb_buf[kpos++] = 0x00; annexb_buf[kpos++] = 0x00; annexb_buf[kpos++] = 0x00; annexb_buf[kpos++] = 0x01;
                                memcpy(annexb_buf + kpos, buf + pps_pos + 4, pps_len); kpos += pps_len;
                                annexb_buf[kpos++] = 0x00; annexb_buf[kpos++] = 0x00; annexb_buf[kpos++] = 0x00; annexb_buf[kpos++] = 0x01;
                                memcpy(annexb_buf + kpos, buf + idr_pos + 4, idr_len); kpos += idr_len;
                                memset(annexb_buf + kpos, 0, AV_INPUT_BUFFER_PADDING_SIZE);

                                uint8_t *cur_ptr = annexb_buf;
                                int cur_size = (int)total_key_size;

                                while (cur_size > 0) {
                                    uint8_t *pout_data = NULL;
                                    int pout_size = 0;
                                    int parsed_len = av_parser_parse2(parser, codec_ctx,
                                                                     &pout_data, &pout_size,
                                                                     cur_ptr, cur_size,
                                                                     AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
                                    if (parsed_len < 0) break;
                                    cur_ptr += parsed_len;
                                    cur_size -= parsed_len;

                                    if (pout_size > 0 && pout_data != NULL) {
                                        if (!muxer_initialized) {
                                            int w = parser->width > 0 ? parser->width : sps_w;
                                            int h = parser->height > 0 ? parser->height : sps_h;
                                            if (w <= 0 || h <= 0) { w = 640; h = 360; }
                                            stats->width = w; stats->height = h;

                                            int ret = avformat_alloc_output_context2(&out_fmt_ctx, NULL, "mp4", config->output_path);
                                            if (ret < 0 || !out_fmt_ctx) goto cleanup_avcc;

                                            out_vstream = avformat_new_stream(out_fmt_ctx, NULL);
                                            if (!out_vstream) goto cleanup_avcc;

                                            out_vstream->codecpar->codec_id = codec_id;
                                            out_vstream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
                                            out_vstream->codecpar->width = w;
                                            out_vstream->codecpar->height = h;
                                            out_vstream->codecpar->format = AV_PIX_FMT_YUV420P;
                                            out_vstream->codecpar->codec_tag = 0;
                                            out_vstream->time_base = (AVRational){1, 90000};

                                            AVDictionary *opts = NULL;
                                            av_dict_set(&opts, "movflags", "faststart", 0);

                                            ret = avio_open(&out_fmt_ctx->pb, config->output_path, AVIO_FLAG_WRITE);
                                            if (ret < 0) { av_dict_free(&opts); goto cleanup_avcc; }

                                            ret = avformat_write_header(out_fmt_ctx, &opts);
                                            av_dict_free(&opts);
                                            if (ret < 0) goto cleanup_avcc;

                                            muxer_initialized = 1;
                                        }

                                        av_packet_unref(pkt);
                                        if (av_new_packet(pkt, pout_size) == 0) {
                                            memcpy(pkt->data, pout_data, pout_size);
                                            int64_t pts_step = 90000 / fps;
                                            pkt->pts = frame_count * pts_step;
                                            pkt->dts = pkt->pts;
                                            pkt->duration = pts_step;
                                            pkt->stream_index = out_vstream->index;

                                            if (parser->key_frame == 1) {
                                                pkt->flags |= AV_PKT_FLAG_KEY;
                                                keyframe_count++;
                                            }

                                            av_interleaved_write_frame(out_fmt_ctx, pkt);
                                            frame_count++;
                                        }
                                    }
                                }

                                pos = idr_pos + 4 + idr_len;
                                frame_emitted = 1;
                            }
                        }
                    }
                }
            }
        }

        // 2. Verificação de Fatia P (Interframe)
        if (!frame_emitted && nal_len >= 10 && nal_len <= 200000 && pos + 4 + nal_len <= buf_len && first_sps_seen) {
            uint8_t b1 = buf[pos + 4];
            uint8_t b2 = buf[pos + 5];
            if (b1 == 0x21 && b2 == 0xE0) {
                if (nal_len + 4 + AV_INPUT_BUFFER_PADDING_SIZE > annexb_buf_size) {
                    annexb_buf_size = nal_len + 65536;
                    annexb_buf = (uint8_t *)realloc(annexb_buf, annexb_buf_size + AV_INPUT_BUFFER_PADDING_SIZE);
                }

                annexb_buf[0] = 0x00; annexb_buf[1] = 0x00; annexb_buf[2] = 0x00; annexb_buf[3] = 0x01;
                memcpy(annexb_buf + 4, buf + pos + 4, nal_len);
                memset(annexb_buf + 4 + nal_len, 0, AV_INPUT_BUFFER_PADDING_SIZE);

                uint8_t *cur_ptr = annexb_buf;
                int cur_size = (int)(nal_len + 4);

                while (cur_size > 0) {
                    uint8_t *pout_data = NULL;
                    int pout_size = 0;

                    int parsed_len = av_parser_parse2(parser, codec_ctx,
                                                     &pout_data, &pout_size,
                                                     cur_ptr, cur_size,
                                                     AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
                    if (parsed_len < 0) break;

                    cur_ptr += parsed_len;
                    cur_size -= parsed_len;

                    if (pout_size > 0 && pout_data != NULL) {
                        if (!muxer_initialized) {
                            int w = parser->width > 0 ? parser->width : sps_w;
                            int h = parser->height > 0 ? parser->height : sps_h;
                            if (w <= 0 || h <= 0) { w = 640; h = 360; }
                            stats->width = w; stats->height = h;

                            int ret = avformat_alloc_output_context2(&out_fmt_ctx, NULL, "mp4", config->output_path);
                            if (ret < 0 || !out_fmt_ctx) goto cleanup_avcc;

                            out_vstream = avformat_new_stream(out_fmt_ctx, NULL);
                            if (!out_vstream) goto cleanup_avcc;

                            out_vstream->codecpar->codec_id = codec_id;
                            out_vstream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
                            out_vstream->codecpar->width = w;
                            out_vstream->codecpar->height = h;
                            out_vstream->codecpar->format = AV_PIX_FMT_YUV420P;
                            out_vstream->codecpar->codec_tag = 0;
                            out_vstream->time_base = (AVRational){1, 90000};

                            AVDictionary *opts = NULL;
                            av_dict_set(&opts, "movflags", "faststart", 0);

                            ret = avio_open(&out_fmt_ctx->pb, config->output_path, AVIO_FLAG_WRITE);
                            if (ret < 0) { av_dict_free(&opts); goto cleanup_avcc; }

                            ret = avformat_write_header(out_fmt_ctx, &opts);
                            av_dict_free(&opts);
                            if (ret < 0) goto cleanup_avcc;

                            muxer_initialized = 1;
                        }

                        av_packet_unref(pkt);
                        if (av_new_packet(pkt, pout_size) == 0) {
                            memcpy(pkt->data, pout_data, pout_size);
                            int64_t pts_step = 90000 / fps;
                            pkt->pts = frame_count * pts_step;
                            pkt->dts = pkt->pts;
                            pkt->duration = pts_step;
                            pkt->stream_index = out_vstream->index;

                            if (parser->key_frame == 1) {
                                pkt->flags |= AV_PKT_FLAG_KEY;
                                keyframe_count++;
                            }

                            av_interleaved_write_frame(out_fmt_ctx, pkt);
                            frame_count++;
                        }
                    }
                }

                pos += 4 + nal_len;
                frame_emitted = 1;
            }
        }

        if (!frame_emitted) {
            stats->dropped_bytes++;
            pos++;
        }

        if (frame_count % 200 == 0) {
            double elapsed = utils_get_time_sec() - stats->start_time;
            int64_t current_file_pos = data_offset + (total_bytes_read - (buf_len - pos));
            utils_print_progress(current_file_pos, file_size, frame_count, elapsed);
        }
    }

    if (muxer_initialized) {
        uint8_t *pout_data = NULL;
        int pout_size = 0;
        av_parser_parse2(parser, codec_ctx, &pout_data, &pout_size, NULL, 0,
                         AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        if (pout_size > 0 && pout_data != NULL) {
            av_packet_unref(pkt);
            if (av_new_packet(pkt, pout_size) == 0) {
                memcpy(pkt->data, pout_data, pout_size);
                int64_t pts_step = 90000 / fps;
                pkt->pts = frame_count * pts_step;
                pkt->dts = pkt->pts;
                pkt->duration = pts_step;
                pkt->stream_index = out_vstream->index;
                if (parser->key_frame == 1) {
                    pkt->flags |= AV_PKT_FLAG_KEY;
                    keyframe_count++;
                }
                av_interleaved_write_frame(out_fmt_ctx, pkt);
                frame_count++;
            }
        }
        av_write_trailer(out_fmt_ctx);
    }

    stats->end_time = utils_get_time_sec();
    stats->processed_bytes = file_size;
    stats->recovered_frames = frame_count;
    stats->keyframes = keyframe_count;

cleanup_avcc:
    free(annexb_buf);
    free(buf);
    av_packet_free(&pkt);
    avcodec_free_context(&codec_ctx);
    av_parser_close(parser);
    if (out_fmt_ctx) {
        if (out_fmt_ctx->pb) {
            avio_closep(&out_fmt_ctx->pb);
        }
        avformat_free_context(out_fmt_ctx);
    }
    fclose(fp);

    return (frame_count > 0) ? 0 : -1;
}

// -----------------------------------------------------------------------------
// Método 4: Desencapsulador Dahua / Intelbras DHAV
// -----------------------------------------------------------------------------
int healer_repair_dhav(const HealerConfig *config, HealerStats *stats) {
    stats->repair_mode_name = "Desencapsulador DHAV Dahua/Intelbras (Método 4)";
    stats->start_time = utils_get_time_sec();

    int64_t file_size = utils_get_file_size(config->input_path);
    if (file_size <= 0) return -1;
    stats->total_bytes = file_size;

    FILE *fp = fopen(config->input_path, "rb");
    if (!fp) return -1;

    enum AVCodecID codec_id = AV_CODEC_ID_H264;
    stats->codec_id = codec_id;

    const AVCodec *decoder = avcodec_find_decoder(codec_id);
    if (!decoder) {
        fclose(fp);
        return -1;
    }

    AVCodecParserContext *parser = av_parser_init(codec_id);
    if (!parser) {
        fclose(fp);
        return -1;
    }

    AVCodecContext *codec_ctx = avcodec_alloc_context3(decoder);
    if (!codec_ctx) {
        av_parser_close(parser);
        fclose(fp);
        return -1;
    }

    AVPacket *pkt = av_packet_alloc();
    AVFormatContext *out_fmt_ctx = NULL;
    AVStream *out_vstream = NULL;
    int muxer_initialized = 0;

    int fps = config->default_fps > 0 ? config->default_fps : 25;
    stats->framerate = (AVRational){fps, 1};

    uint8_t *chunk_buf = (uint8_t *)malloc(CHUNK_BUFFER_SIZE + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!chunk_buf) {
        av_packet_free(&pkt);
        avcodec_free_context(&codec_ctx);
        av_parser_close(parser);
        fclose(fp);
        return -1;
    }

    int64_t current_processed = 0;
    int64_t frame_count = 0;
    int64_t keyframe_count = 0;

    while (!feof(fp)) {
        size_t bytes_read = fread(chunk_buf, 1, CHUNK_BUFFER_SIZE, fp);
        if (bytes_read == 0) break;

        // Limpeza dos cabeçalhos DHAV (substitui DHAV por padding ou avança)
        for (size_t i = 0; i + 4 <= bytes_read; i++) {
            if (memcmp(chunk_buf + i, "DHAV", 4) == 0) {
                // Em arquivos DHAV, os primeiros 16 a 24 bytes são metadados proprietários
                // Se após o cabeçalho houver start code Annex B, preserva o fluxo
                size_t dhav_hdr_len = (i + 24 <= bytes_read) ? 24 : 16;
                memset(chunk_buf + i, 0, dhav_hdr_len);
                i += dhav_hdr_len - 1;
            }
        }

        uint8_t *cur_ptr = chunk_buf;
        int cur_size = (int)bytes_read;

        while (cur_size > 0) {
            uint8_t *pout_data = NULL;
            int pout_size = 0;

            int len = av_parser_parse2(parser, codec_ctx,
                                       &pout_data, &pout_size,
                                       cur_ptr, cur_size,
                                       AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
            if (len < 0) {
                cur_ptr++;
                cur_size--;
                stats->dropped_bytes++;
                continue;
            }

            cur_ptr += len;
            cur_size -= len;

            if (pout_size > 0 && pout_data != NULL) {
                if (!muxer_initialized) {
                    int w = parser->width > 0 ? parser->width : 1920;
                    int h = parser->height > 0 ? parser->height : 1080;
                    stats->width = w;
                    stats->height = h;

                    int ret = avformat_alloc_output_context2(&out_fmt_ctx, NULL, "mp4", config->output_path);
                    if (ret < 0 || !out_fmt_ctx) goto cleanup_dhav;

                    out_vstream = avformat_new_stream(out_fmt_ctx, NULL);
                    if (!out_vstream) goto cleanup_dhav;

                    out_vstream->codecpar->codec_id = codec_id;
                    out_vstream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
                    out_vstream->codecpar->width = w;
                    out_vstream->codecpar->height = h;
                    out_vstream->codecpar->format = AV_PIX_FMT_YUV420P;
                    out_vstream->codecpar->codec_tag = 0;
                    out_vstream->time_base = (AVRational){1, 90000};

                    AVDictionary *opts = NULL;
                    av_dict_set(&opts, "movflags", "faststart", 0);

                    ret = avio_open(&out_fmt_ctx->pb, config->output_path, AVIO_FLAG_WRITE);
                    if (ret < 0) {
                        av_dict_free(&opts);
                        goto cleanup_dhav;
                    }

                    ret = avformat_write_header(out_fmt_ctx, &opts);
                    av_dict_free(&opts);
                    if (ret < 0) goto cleanup_dhav;

                    muxer_initialized = 1;
                }

                av_packet_unref(pkt);
                if (av_new_packet(pkt, pout_size) == 0) {
                    memcpy(pkt->data, pout_data, pout_size);
                    int64_t pts_step = 90000 / fps;
                    pkt->pts = frame_count * pts_step;
                    pkt->dts = pkt->pts;
                    pkt->duration = pts_step;
                    pkt->stream_index = out_vstream->index;

                    if (parser->key_frame == 1) {
                        pkt->flags |= AV_PKT_FLAG_KEY;
                        keyframe_count++;
                    }

                    av_interleaved_write_frame(out_fmt_ctx, pkt);
                    frame_count++;
                }
            }
        }

        current_processed += bytes_read;
        double elapsed = utils_get_time_sec() - stats->start_time;
        utils_print_progress(current_processed, file_size, frame_count, elapsed);
    }

    if (muxer_initialized) {
        av_write_trailer(out_fmt_ctx);
    }

    stats->end_time = utils_get_time_sec();
    stats->processed_bytes = current_processed;
    stats->recovered_frames = frame_count;
    stats->keyframes = keyframe_count;

cleanup_dhav:
    free(chunk_buf);
    av_packet_free(&pkt);
    avcodec_free_context(&codec_ctx);
    av_parser_close(parser);
    if (out_fmt_ctx) {
        if (out_fmt_ctx->pb) {
            avio_closep(&out_fmt_ctx->pb);
        }
        avformat_free_context(out_fmt_ctx);
    }
    fclose(fp);

    return (frame_count > 0) ? 0 : -1;
}

// -----------------------------------------------------------------------------
// Método 5: Injeção de Parâmetros SPS/PPS Sintéticos Heurísticos
// -----------------------------------------------------------------------------
int healer_repair_synthetic_params(const HealerConfig *config, HealerStats *stats) {
    stats->repair_mode_name = "Injeção de Parâmetros Sintéticos Heurísticos (Método 5)";
    stats->start_time = utils_get_time_sec();

    int64_t file_size = utils_get_file_size(config->input_path);
    if (file_size <= 0) return -1;
    stats->total_bytes = file_size;

    FILE *fp = fopen(config->input_path, "rb");
    if (!fp) return -1;

    int64_t data_offset = 0;
    int64_t data_size = file_size;
    nal_locate_mdat(fp, file_size, &data_offset, &data_size);

#ifdef _WIN32
    _fseeki64(fp, data_offset, SEEK_SET);
#else
    fseeko(fp, data_offset, SEEK_SET);
#endif

    enum AVCodecID codec_id = AV_CODEC_ID_H264;
    stats->codec_id = codec_id;

    // Gera SPS/PPS sintético Baseline 1080p
    uint8_t *synth_sps_pps = NULL;
    int synth_size = 0;
    nal_generate_synthetic_sps_pps(1920, 1080, 25, &synth_sps_pps, &synth_size);
    if (!synth_sps_pps || synth_size <= 0) {
        fclose(fp);
        return -1;
    }

    const AVCodec *decoder = avcodec_find_decoder(codec_id);
    if (!decoder) {
        free(synth_sps_pps);
        fclose(fp);
        return -1;
    }

    AVCodecParserContext *parser = av_parser_init(codec_id);
    if (!parser) {
        free(synth_sps_pps);
        fclose(fp);
        return -1;
    }

    AVCodecContext *codec_ctx = avcodec_alloc_context3(decoder);
    if (!codec_ctx) {
        free(synth_sps_pps);
        av_parser_close(parser);
        fclose(fp);
        return -1;
    }

    AVPacket *pkt = av_packet_alloc();
    AVFormatContext *out_fmt_ctx = NULL;
    AVStream *out_vstream = NULL;

    int fps = config->default_fps > 0 ? config->default_fps : 25;
    stats->framerate = (AVRational){fps, 1};
    stats->width = 1920;
    stats->height = 1080;

    int ret = avformat_alloc_output_context2(&out_fmt_ctx, NULL, "mp4", config->output_path);
    if (ret < 0 || !out_fmt_ctx) {
        free(synth_sps_pps);
        av_packet_free(&pkt);
        avcodec_free_context(&codec_ctx);
        av_parser_close(parser);
        fclose(fp);
        return -1;
    }

    out_vstream = avformat_new_stream(out_fmt_ctx, NULL);
    if (!out_vstream) {
        free(synth_sps_pps);
        av_packet_free(&pkt);
        avcodec_free_context(&codec_ctx);
        av_parser_close(parser);
        avformat_free_context(out_fmt_ctx);
        fclose(fp);
        return -1;
    }

    out_vstream->codecpar->codec_id = codec_id;
    out_vstream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    out_vstream->codecpar->width = 1920;
    out_vstream->codecpar->height = 1080;
    out_vstream->codecpar->format = AV_PIX_FMT_YUV420P;
    out_vstream->codecpar->codec_tag = 0;
    out_vstream->time_base = (AVRational){1, 90000};

    AVDictionary *opts = NULL;
    av_dict_set(&opts, "movflags", "faststart", 0);

    ret = avio_open(&out_fmt_ctx->pb, config->output_path, AVIO_FLAG_WRITE);
    if (ret < 0) {
        av_dict_free(&opts);
        free(synth_sps_pps);
        av_packet_free(&pkt);
        avcodec_free_context(&codec_ctx);
        av_parser_close(parser);
        avformat_free_context(out_fmt_ctx);
        fclose(fp);
        return -1;
    }

    ret = avformat_write_header(out_fmt_ctx, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        free(synth_sps_pps);
        av_packet_free(&pkt);
        avcodec_free_context(&codec_ctx);
        av_parser_close(parser);
        avio_closep(&out_fmt_ctx->pb);
        avformat_free_context(out_fmt_ctx);
        fclose(fp);
        return -1;
    }

    uint8_t *chunk_buf = (uint8_t *)malloc(CHUNK_BUFFER_SIZE + AV_INPUT_BUFFER_PADDING_SIZE);
    int64_t current_processed = data_offset;
    int64_t frame_count = 0;
    int64_t keyframe_count = 0;
    int64_t pts_step = 90000 / fps;

    while (!feof(fp)) {
        size_t bytes_read = fread(chunk_buf, 1, CHUNK_BUFFER_SIZE, fp);
        if (bytes_read == 0) break;

        memset(chunk_buf + bytes_read, 0, AV_INPUT_BUFFER_PADDING_SIZE);

        uint8_t *cur_ptr = chunk_buf;
        int cur_size = (int)bytes_read;

        while (cur_size > 0) {
            uint8_t *pout_data = NULL;
            int pout_size = 0;

            int len = av_parser_parse2(parser, codec_ctx,
                                       &pout_data, &pout_size,
                                       cur_ptr, cur_size,
                                       AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
            if (len < 0) {
                cur_ptr++;
                cur_size--;
                stats->dropped_bytes++;
                continue;
            }

            cur_ptr += len;
            cur_size -= len;

            if (pout_size > 0 && pout_data != NULL) {
                av_packet_unref(pkt);

                if (frame_count == 0 && synth_sps_pps && synth_size > 0) {
                    if (av_new_packet(pkt, synth_size + pout_size) == 0) {
                        memcpy(pkt->data, synth_sps_pps, synth_size);
                        memcpy(pkt->data + synth_size, pout_data, pout_size);
                    }
                } else {
                    if (av_new_packet(pkt, pout_size) == 0) {
                        memcpy(pkt->data, pout_data, pout_size);
                    }
                }

                pkt->pts = frame_count * pts_step;
                pkt->dts = pkt->pts;
                pkt->duration = pts_step;
                pkt->stream_index = out_vstream->index;

                if (parser->key_frame == 1) {
                    pkt->flags |= AV_PKT_FLAG_KEY;
                    keyframe_count++;
                }

                av_interleaved_write_frame(out_fmt_ctx, pkt);
                frame_count++;
            }
        }

        current_processed += bytes_read;
        double elapsed = utils_get_time_sec() - stats->start_time;
        utils_print_progress(current_processed, file_size, frame_count, elapsed);
    }

    av_write_trailer(out_fmt_ctx);

    stats->end_time = utils_get_time_sec();
    stats->processed_bytes = current_processed;
    stats->recovered_frames = frame_count;
    stats->keyframes = keyframe_count;

    free(synth_sps_pps);
    free(chunk_buf);
    av_packet_free(&pkt);
    avcodec_free_context(&codec_ctx);
    av_parser_close(parser);
    avio_closep(&out_fmt_ctx->pb);
    avformat_free_context(out_fmt_ctx);
    fclose(fp);

    return (frame_count > 0) ? 0 : -1;
}

// -----------------------------------------------------------------------------
// Método de Fallback / Modo Referência: Transplante de Metadados / SPS / PPS
// -----------------------------------------------------------------------------
int healer_repair_with_reference(const HealerConfig *config, HealerStats *stats) {
    stats->repair_mode_name = "Transplante de Metadados / SPS / PPS (Modo Referência)";
    stats->start_time = utils_get_time_sec();

    if (!config->reference_path[0]) {
        fprintf(stderr, "[-] Erro: Arquivo de referência não informado.\n");
        return -1;
    }

    int64_t file_size = utils_get_file_size(config->input_path);
    if (file_size <= 0) {
        fprintf(stderr, "[-] Erro: Arquivo corrompido '%s' está vazio ou inacessível.\n", config->input_path);
        return -1;
    }
    stats->total_bytes = file_size;

    AVFormatContext *ref_fmt_ctx = NULL;
    int ret = avformat_open_input(&ref_fmt_ctx, config->reference_path, NULL, NULL);
    if (ret < 0) {
        char err_buf[128];
        av_strerror(ret, err_buf, sizeof(err_buf));
        fprintf(stderr, "[-] Erro: Não foi possível abrir o vídeo de referência '%s': %s\n", config->reference_path, err_buf);
        return -1;
    }

    ret = avformat_find_stream_info(ref_fmt_ctx, NULL);
    if (ret < 0) {
        avformat_close_input(&ref_fmt_ctx);
        return -1;
    }

    int ref_v_idx = av_find_best_stream(ref_fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (ref_v_idx < 0) {
        avformat_close_input(&ref_fmt_ctx);
        return -1;
    }

    AVStream *ref_vstream = ref_fmt_ctx->streams[ref_v_idx];
    AVCodecParameters *ref_codecpar = ref_vstream->codecpar;

    stats->codec_id = ref_codecpar->codec_id;
    stats->width = ref_codecpar->width;
    stats->height = ref_codecpar->height;

    int fps = 25;
    if (ref_vstream->avg_frame_rate.num > 0 && ref_vstream->avg_frame_rate.den > 0) {
        fps = ref_vstream->avg_frame_rate.num / ref_vstream->avg_frame_rate.den;
        stats->framerate = ref_vstream->avg_frame_rate;
    } else if (ref_vstream->r_frame_rate.num > 0 && ref_vstream->r_frame_rate.den > 0) {
        fps = ref_vstream->r_frame_rate.num / ref_vstream->r_frame_rate.den;
        stats->framerate = ref_vstream->r_frame_rate;
    } else {
        stats->framerate = (AVRational){fps, 1};
    }
    if (fps <= 0) fps = 25;

    uint8_t *ref_sps_pps = NULL;
    int ref_sps_pps_size = 0;
    if (ref_codecpar->extradata && ref_codecpar->extradata_size > 0) {
        extract_sps_pps_from_extradata(ref_codecpar->codec_id,
                                       ref_codecpar->extradata,
                                       ref_codecpar->extradata_size,
                                       &ref_sps_pps, &ref_sps_pps_size);
    }

    if (config->verbose) {
        fprintf(stdout, "[+] Vídeo de Referência: %s\n", config->reference_path);
        fprintf(stdout, "[+] Transplante de Trilha: %s, Resolução: %dx%d, Taxa: %d fps\n",
                utils_codec_name(ref_codecpar->codec_id), ref_codecpar->width, ref_codecpar->height, fps);
        if (ref_sps_pps_size > 0) {
            fprintf(stdout, "[+] Extraídos %d bytes de parâmetros SPS/PPS Annex B da referência.\n", ref_sps_pps_size);
        }
    }

    AVFormatContext *out_fmt_ctx = NULL;
    ret = avformat_alloc_output_context2(&out_fmt_ctx, NULL, "mp4", config->output_path);
    if (ret < 0 || !out_fmt_ctx) {
        if (ref_sps_pps) free(ref_sps_pps);
        avformat_close_input(&ref_fmt_ctx);
        return -1;
    }

    AVStream *out_vstream = avformat_new_stream(out_fmt_ctx, NULL);
    if (!out_vstream) {
        if (ref_sps_pps) free(ref_sps_pps);
        avformat_free_context(out_fmt_ctx);
        avformat_close_input(&ref_fmt_ctx);
        return -1;
    }

    out_vstream->codecpar->codec_id = ref_codecpar->codec_id;
    out_vstream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    out_vstream->codecpar->width = ref_codecpar->width;
    out_vstream->codecpar->height = ref_codecpar->height;
    out_vstream->codecpar->format = ref_codecpar->format != AV_PIX_FMT_NONE ? ref_codecpar->format : AV_PIX_FMT_YUV420P;
    out_vstream->codecpar->codec_tag = 0;
    out_vstream->time_base = (AVRational){1, 90000};

    AVDictionary *opts = NULL;
    av_dict_set(&opts, "movflags", "faststart", 0);

    ret = avio_open(&out_fmt_ctx->pb, config->output_path, AVIO_FLAG_WRITE);
    if (ret < 0) {
        av_dict_free(&opts);
        if (ref_sps_pps) free(ref_sps_pps);
        avformat_free_context(out_fmt_ctx);
        avformat_close_input(&ref_fmt_ctx);
        return -1;
    }

    ret = avformat_write_header(out_fmt_ctx, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        if (ref_sps_pps) free(ref_sps_pps);
        avio_closep(&out_fmt_ctx->pb);
        avformat_free_context(out_fmt_ctx);
        avformat_close_input(&ref_fmt_ctx);
        return -1;
    }

    FILE *fp = fopen(config->input_path, "rb");
    if (!fp) {
        if (ref_sps_pps) free(ref_sps_pps);
        avio_closep(&out_fmt_ctx->pb);
        avformat_free_context(out_fmt_ctx);
        avformat_close_input(&ref_fmt_ctx);
        return -1;
    }

    int64_t data_offset = 0;
    int64_t data_size = file_size;
    nal_locate_mdat(fp, file_size, &data_offset, &data_size);

#ifdef _WIN32
    _fseeki64(fp, data_offset, SEEK_SET);
#else
    fseeko(fp, data_offset, SEEK_SET);
#endif

    const AVCodec *decoder = avcodec_find_decoder(ref_codecpar->codec_id);
    AVCodecParserContext *parser = av_parser_init(ref_codecpar->codec_id);
    AVCodecContext *codec_ctx = avcodec_alloc_context3(decoder);
    AVPacket *pkt = av_packet_alloc();

    uint8_t *chunk_buf = (uint8_t *)malloc(CHUNK_BUFFER_SIZE + AV_INPUT_BUFFER_PADDING_SIZE);
    int64_t current_processed = data_offset;
    int64_t frame_count = 0;
    int64_t keyframe_count = 0;
    int64_t pts_step = 90000 / fps;

    size_t probe_len = 65536;
    if ((int64_t)probe_len > data_size) probe_len = (size_t)data_size;
    uint8_t probe_peek[65536];
    size_t peek_read = fread(probe_peek, 1, probe_len, fp);
    int avcc_mode = is_avcc_stream(probe_peek, peek_read);

#ifdef _WIN32
    _fseeki64(fp, data_offset, SEEK_SET);
#else
    fseeko(fp, data_offset, SEEK_SET);
#endif

    while (!feof(fp)) {
        size_t bytes_read = fread(chunk_buf, 1, CHUNK_BUFFER_SIZE, fp);
        if (bytes_read == 0) break;

        memset(chunk_buf + bytes_read, 0, AV_INPUT_BUFFER_PADDING_SIZE);

        if (avcc_mode) {
            size_t idx = 0;
            while (idx + 4 < bytes_read) {
                uint32_t nal_len = ((uint32_t)chunk_buf[idx] << 24) |
                                   ((uint32_t)chunk_buf[idx+1] << 16) |
                                   ((uint32_t)chunk_buf[idx+2] << 8) |
                                   chunk_buf[idx+3];
                int is_valid = 0;
                if (nal_len >= 4 && nal_len <= 500000 && idx + 4 + nal_len <= bytes_read) {
                    uint8_t b1 = chunk_buf[idx + 4];
                    if ((b1 & 0x80) == 0) {
                        uint8_t t = b1 & 0x1F;
                        if ((t == H264_NAL_SPS && nal_len <= 128) ||
                            (t == H264_NAL_PPS && nal_len <= 64) ||
                            (t == H264_NAL_IDR_SLICE && nal_len >= 20 && (chunk_buf[idx + 5] & 0x80) != 0) ||
                            (t == H264_NAL_SLICE && nal_len >= 10 && (chunk_buf[idx + 5] & 0x80) != 0) ||
                            ((t == H264_NAL_SEI || t == H264_NAL_AUD) && nal_len <= 2048)) {
                            is_valid = 1;
                        }
                    }
                }
                if (is_valid) {
                    chunk_buf[idx]   = 0x00;
                    chunk_buf[idx+1] = 0x00;
                    chunk_buf[idx+2] = 0x00;
                    chunk_buf[idx+3] = 0x01;
                    idx += 4 + nal_len;
                } else {
                    idx++;
                }
            }
        }

        uint8_t *cur_ptr = chunk_buf;
        int cur_size = (int)bytes_read;

        while (cur_size > 0) {
            uint8_t *pout_data = NULL;
            int pout_size = 0;

            int len = av_parser_parse2(parser, codec_ctx,
                                       &pout_data, &pout_size,
                                       cur_ptr, cur_size,
                                       AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
            if (len < 0) {
                cur_ptr++;
                cur_size--;
                stats->dropped_bytes++;
                continue;
            }

            cur_ptr += len;
            cur_size -= len;

            if (pout_size > 0 && pout_data != NULL) {
                av_packet_unref(pkt);

                if (frame_count == 0 && ref_sps_pps && ref_sps_pps_size > 0) {
                    if (av_new_packet(pkt, ref_sps_pps_size + pout_size) == 0) {
                        memcpy(pkt->data, ref_sps_pps, ref_sps_pps_size);
                        memcpy(pkt->data + ref_sps_pps_size, pout_data, pout_size);
                    }
                } else {
                    if (av_new_packet(pkt, pout_size) == 0) {
                        memcpy(pkt->data, pout_data, pout_size);
                    }
                }

                pkt->pts = frame_count * pts_step;
                pkt->dts = pkt->pts;
                pkt->duration = pts_step;
                pkt->stream_index = out_vstream->index;

                if (parser->key_frame == 1) {
                    pkt->flags |= AV_PKT_FLAG_KEY;
                    keyframe_count++;
                }

                av_interleaved_write_frame(out_fmt_ctx, pkt);
                frame_count++;
            }
        }

        current_processed += bytes_read;
        double elapsed = utils_get_time_sec() - stats->start_time;
        utils_print_progress(current_processed, file_size, frame_count, elapsed);
    }

    av_write_trailer(out_fmt_ctx);

    stats->end_time = utils_get_time_sec();
    stats->processed_bytes = current_processed;
    stats->recovered_frames = frame_count;
    stats->keyframes = keyframe_count;

    if (ref_sps_pps) free(ref_sps_pps);
    free(chunk_buf);
    av_packet_free(&pkt);
    avcodec_free_context(&codec_ctx);
    av_parser_close(parser);
    fclose(fp);
    avio_closep(&out_fmt_ctx->pb);
    avformat_free_context(out_fmt_ctx);
    avformat_close_input(&ref_fmt_ctx);

    return (frame_count > 0) ? 0 : -1;
}

// -----------------------------------------------------------------------------
// Ponto de Entrada Principal: Cascata Autônoma Inteligente
// -----------------------------------------------------------------------------
int healer_process(const HealerConfig *config, HealerStats *stats) {
    memset(stats, 0, sizeof(HealerStats));

    // Se o usuário explicitamente informou o vídeo de referência, tenta o transplante primeiro
    if (config->reference_path[0] && !config->force_raw_scan) {
        fprintf(stdout, "[*] Modo Referência Detectado: Executando Transplante de Parâmetros SPS/PPS...\n");
        int ret = healer_repair_with_reference(config, stats);
        if (ret == 0 && stats->recovered_frames > 0) {
            return HEALER_SUCCESS;
        }
        fprintf(stderr, "\n[!] Transplante via referência não recuperou quadros. Iniciando cascata autônoma interna...\n");
    }

    // 1. Diagnóstico Forense em Tempo Real do Arquivo Danificado
    ForensicProbe probe;
    nal_forensic_probe(config->input_path, &probe, stats->forensic_summary, sizeof(stats->forensic_summary));

    fprintf(stdout, "\n======================================================================\n");
    fprintf(stdout, "          DIAGNÓSTICO FORENSE AUTOMÁTICO - FixCFTV                    \n");
    fprintf(stdout, "======================================================================\n");
    fprintf(stdout, "%s\n", stats->forensic_summary);
    fprintf(stdout, "======================================================================\n\n");

    // Se o usuário forçou a varredura bruta
    if (config->force_raw_scan) {
        fprintf(stdout, "[*] Varredura Bruta forçada pelo usuário (-f)...\n");
        int ret = healer_repair_raw_scan(config, stats);
        return (ret == 0 && stats->recovered_frames > 0) ? HEALER_SUCCESS : HEALER_ERR_FAILED;
    }

    // 2. Cascata Inteligente de Métodos Internos (Sem Necessidade de Referência Inicial)

    // Tentativa A: Formato Proprietário Dahua / Intelbras DHAV
    if (probe.is_dhav) {
        fprintf(stdout, "[*] Cascata [1/5]: Detectada assinatura DHAV. Executando Desencapsulador DHAV...\n");
        int ret = healer_repair_dhav(config, stats);
        if (ret == 0 && stats->recovered_frames > 0) {
            return HEALER_SUCCESS;
        }
    }

    // Tentativa B: Restauração Permissiva de Contêiner (se houver fragmentos ou moov recuperável)
    if (probe.is_mp4_container && !probe.has_mdat) {
        fprintf(stdout, "[*] Cascata [2/5]: Tentando Restauração Permissiva de Contêiner e Re-indexação...\n");
        int ret = healer_repair_permissive_container(config, stats);
        if (ret == 0 && stats->recovered_frames > 0) {
            return HEALER_SUCCESS;
        }
    }

    // Tentativa C: Stream AVCC Intercalado / MP4 Truncado (se is_avcc ou is_mp4_container com mdat)
    if (probe.is_avcc || (probe.is_mp4_container && probe.has_mdat)) {
        fprintf(stdout, "[*] Cascata [3/5]: Detectado Stream AVCC / Contêiner MP4 truncado. Executando Reconstrução AVCC...\n");
        int ret = healer_repair_avcc(config, stats);
        if (ret == 0 && stats->recovered_frames > 0) {
            return HEALER_SUCCESS;
        }
    }

    // Tentativa D: Varredura Bruta de NAL Units Annex B com Parâmetros In-Stream
    fprintf(stdout, "[*] Cascata [4/5]: Executando Varredura Bruta de Unidades NAL Annex B e Reconstrução FastStart...\n");
    int ret = healer_repair_raw_scan(config, stats);
    if (ret == 0 && stats->recovered_frames > 0) {
        return HEALER_SUCCESS;
    }

    // Tentativa E: Injeção de Parâmetros Sintéticos Heurísticos (quando o vídeo perdeu todo SPS/PPS)
    if (probe.slice_count > 0 || probe.idr_count > 0) {
        fprintf(stdout, "[*] Cascata [5/5]: Detectadas fatias de vídeo sem cabeçalhos. Injetando parâmetros sintéticos...\n");
        ret = healer_repair_synthetic_params(config, stats);
        if (ret == 0 && stats->recovered_frames > 0) {
            return HEALER_SUCCESS;
        }
    }

    // 3. Fallback: Todos os métodos internos falharam por falta de descritores de codec do sensor
    stats->needs_reference = 1;
    fprintf(stderr, "\n======================================================================\n");
    fprintf(stderr, "[-] RESULTADO: Todos os 5 métodos internos de recuperação autônoma falharam.\n");
    fprintf(stderr, "[-] O arquivo de vídeo perdeu completamente a tabela de parâmetros do sensor óptico.\n");
    fprintf(stderr, "[*] SOLUÇÃO NECESSÁRIA:\n");
    fprintf(stderr, "    Por favor, forneça um vídeo de poucos segundos gravado pelo mesmo DVR / câmera\n");
    fprintf(stderr, "    como referência para transplante de metadados: -r <arquivo_bom.mp4>\n");
    fprintf(stderr, "======================================================================\n");

    return HEALER_ERR_NEEDS_REFERENCE;
}
