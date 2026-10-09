#pragma once
#include "FileStreamingManager.h"
#include "core_json.h"

#define OPT_VERSION "1"

namespace chompi {
class OptionsManager 
{
    public:
    OptionsManager() {}
    ~OptionsManager() {}

    void StrAppend(char* buffer, char* append)
    {
        size_t len = strlen(buffer);
        size_t app_len = strlen(append);

        for(size_t i = 0; i < app_len; i++)
        {
            buffer[len + i] = append[i];
        }
        buffer[len + app_len] = '\0';
    }

    /** Init with defaults,
        open the file and parse, if valid overriding defaults,
        write a new file no matter what
    */
    void Init()
    {
        // fill in defaults
        record_latch = false;
        midi_ch_in = 0;
        midi_ch_out_chroma = 0;
        midi_ch_out_slice = 1;
        midi_clock_out = true;
        monitor_position = 0;
        pitch_shift_quantization = true;
        midi_cc_in = true;
        midi_cc_out = true;
        transport_type = 0;
        delay_mute = false;
        loop_quantize_grid = 16;      // 1/16 notes
        loop_quantize_strength = 100; // %

        /** TODO: make sure the open settings are correct */
        const char fname[32] = "options.json";

        FRESULT res = f_stat(fname, nullptr);

        // create if not exist, don't overwrite
        f_open(&fptr_opt, fname, (FA_OPEN_ALWAYS | FA_WRITE | FA_READ));

        UINT br;
        f_read(&fptr_opt, &opt_file[0], kOptFileSize, &br);

        if(res == FR_OK)
            Parse();

        WriteFile();
    }

    /** Write a JSON file with whatever settings we have store locally
        These might be defaults, or whatever we parsed from the file most recently
    */
    void WriteFile()
    {
        // header
        std::fill_n(opt_file, kOptFileSize, '\0');
        strcpy(opt_file, "{\n\t\"chompi\": [\n\t\t{\n\t\t\t\"name\": \"Record Latch\",\n\t\t\t\"value\": ");

        // rec latch
        char append[72];
        sprintf(append, "%s", record_latch ? "true" : "false");
        StrAppend(opt_file, append);

        // midi in channel
        sprintf(append, "\n\t\t},\n\t\t{\n\t\t\t\"name\": \"Midi In Channel\",\n\t\t\t\"value\": ");
        StrAppend(opt_file, append);

        sprintf(append, "%d", midi_ch_in + 1);
        StrAppend(opt_file, append);

        // midi out channels
        sprintf(append, "\n\t\t},\n\t\t{\n\t\t\t\"name\": \"Midi Out Channel Chromatic\",\n\t\t\t\"value\": ");
        StrAppend(opt_file, append);

        sprintf(append, "%d", int(midi_ch_out_chroma + 1));
        StrAppend(opt_file, append);

        sprintf(append, "\n\t\t},\n\t\t{\n\t\t\t\"name\": \"Midi Out Channel Slice\",\n\t\t\t\"value\": ");
        StrAppend(opt_file, append);

        sprintf(append, "%d", int(midi_ch_out_slice + 1));
        StrAppend(opt_file, append);

        // midi clock out toggle
        sprintf(append, "\n\t\t},\n\t\t{\n\t\t\t\"name\": \"MIDI Clock Out\",\n\t\t\t\"value\": ");
        StrAppend(opt_file, append);

        sprintf(append, "%s", midi_clock_out ? "true" : "false");
        StrAppend(opt_file, append);

        // Monitor Position
        sprintf(append, "\n\t\t},\n\t\t{\n\t\t\t\"name\": \"Monitor Position\",\n\t\t\t\"value\": ");
        StrAppend(opt_file, append);

        sprintf(append, "%d", int(monitor_position + 1));
        StrAppend(opt_file, append);

        // Pitch Shift Quantization. true == quant in shift menu (default), false == quant in normal menu
        sprintf(append, "\n\t\t},\n\t\t{\n\t\t\t\"name\": \"Pitch Quantize In Shift Menu\",\n\t\t\t\"value\": ");
        StrAppend(opt_file, append);

        sprintf(append, "%s", pitch_shift_quantization ? "true" : "false");
        StrAppend(opt_file, append);

        sprintf(append, "\n\t\t},\n\t\t{\n\t\t\t\"name\": \"MIDI CC In\",\n\t\t\t\"value\": ");
        StrAppend(opt_file, append);

        sprintf(append, "%s", midi_cc_in ? "true" : "false");
        StrAppend(opt_file, append);

        sprintf(append, "\n\t\t},\n\t\t{\n\t\t\t\"name\": \"MIDI CC Out\",\n\t\t\t\"value\": ");
        StrAppend(opt_file, append);

        sprintf(append, "%s", midi_cc_out ? "true" : "false");
        StrAppend(opt_file, append);

        // Transport Behavior
        sprintf(append, "\n\t\t},\n\t\t{\n\t\t\t\"name\": \"Midi Start-Stop Message Behavior\",\n\t\t\t\"value\": ");
        StrAppend(opt_file, append);

        sprintf(append, "%d", int(transport_type + 1));
        StrAppend(opt_file, append);

        sprintf(append, "\n\t\t},\n\t\t{\n\t\t\t\"name\": \"Delay Buffer Unfreeze Mute\",\n\t\t\t\"value\": ");
        StrAppend(opt_file, append);

        sprintf(append, "%s", delay_mute ? "true" : "false");
        StrAppend(opt_file, append);

        // Looper quantize grid: notes per whole note (4, 8, 12, 16, 24, 32), 0 = off
        sprintf(append, "\n\t\t},\n\t\t{\n\t\t\t\"name\": \"Loop Quantize Grid\",\n\t\t\t\"value\": ");
        StrAppend(opt_file, append);

        sprintf(append, "%d", int(loop_quantize_grid));
        StrAppend(opt_file, append);

        // Looper quantize strength: 0-100 (%)
        sprintf(append, "\n\t\t},\n\t\t{\n\t\t\t\"name\": \"Loop Quantize Strength\",\n\t\t\t\"value\": ");
        StrAppend(opt_file, append);

        sprintf(append, "%d", int(loop_quantize_strength));
        StrAppend(opt_file, append);

        // footer
        sprintf(append, "\n\t\t}\n\t]\n}");
        StrAppend(opt_file, append);

        // write the file
        UINT bw = 0;

        f_lseek(&fptr_opt, 0);
        f_write(&fptr_opt, opt_file, strlen(opt_file), &bw);
        f_truncate(&fptr_opt);
        f_sync(&fptr_opt);
    }

    /** loads the JSON file, storing in name/value pairs for all keys */
    void Parse()
    {
        JSONStatus_t json_res;

        size_t len = strlen(opt_file);
        json_res = JSON_Validate(opt_file, len);

        // is the file valid
        if(json_res == JSONSuccess)
        {
            char   query[51];
            char*  value;
            size_t value_len;

            /** TODO: ? Parse version number. Don't think we'll actually need this tbh  */

            for(size_t i = 0; i < kNumOptions; i++)
            {
                sprintf(query, "chompi[%d].name", i);
                json_res = JSON_Search(
                    opt_file, len, query, strlen(query), &value, &value_len);

                // The file can hold fewer entries than kNumOptions (e.g. an options.json
                // from stock TEMPO, which has no looper settings). Stop here rather than
                // touch an unset pointer.
                if(json_res != JSONSuccess)
                    break;

                char save = value[value_len];
                value[value_len] = '\0';

                int field = -1;

                if(strcmp(value, "Record Latch") == 0 && json_res == JSONSuccess)
                    field = 0;
                if(strcmp(value, "Midi In Channel") == 0 && json_res == JSONSuccess)
                    field = 1;
                if(strcmp(value, "Midi Out Channel Chromatic") == 0 && json_res == JSONSuccess)
                    field = 2;
                if(strcmp(value, "Midi Out Channel Slice") == 0 && json_res == JSONSuccess)
                    field = 3;
                if(strcmp(value, "MIDI Clock Out") == 0 && json_res == JSONSuccess)
                    field = 4;
                if(strcmp(value, "Monitor Position") == 0 && json_res == JSONSuccess)
                    field = 5;
                if(strcmp(value, "Pitch Quantize In Shift Menu") == 0 && json_res == JSONSuccess)
                    field = 6;
                if(strcmp(value, "MIDI CC In") == 0 && json_res == JSONSuccess)
                    field = 7;
                if(strcmp(value, "MIDI CC Out") == 0 && json_res == JSONSuccess)
                    field = 8;
                if(strcmp(value, "Midi Start-Stop Message Behavior") == 0 && json_res == JSONSuccess)
                    field = 9;
                if(strcmp(value, "Delay Buffer Unfreeze Mute") == 0 && json_res == JSONSuccess)
                    field = 10;
                if(strcmp(value, "Loop Quantize Grid") == 0 && json_res == JSONSuccess)
                    field = 11;
                if(strcmp(value, "Loop Quantize Strength") == 0 && json_res == JSONSuccess)
                    field = 12;

                value[value_len] = save;

                if(field == 0 || field == 4 || field == 6 || field == 7 || field == 8 || field == 10)
                {
                    sprintf(query, "chompi[%d].value", i);
                    json_res = JSON_Search(
                        opt_file, len, query, strlen(query), &value, &value_len);
                    save = value[value_len];
                    value[value_len] = '\0';

                    if(strcmp(value, "true") == 0 && json_res == JSONSuccess && field == 0)
                        record_latch = true;
                    else if(strcmp(value, "false") == 0 && json_res == JSONSuccess && field == 4)
                        midi_clock_out = false;
                    else if(strcmp(value, "false") == 0 && json_res == JSONSuccess && field == 6)
                        pitch_shift_quantization = false;
                    else if(strcmp(value, "false") == 0 && json_res == JSONSuccess && field == 7)
                        midi_cc_in = false;
                    else if(strcmp(value, "false") == 0 && json_res == JSONSuccess && field == 8)
                        midi_cc_out = false;
                    else if(strcmp(value, "true") == 0 && json_res == JSONSuccess && field == 10)
                        delay_mute = true;

                    value[value_len] = save;
                }
                else if(field == 11 || field == 12)
                {
                    sprintf(query, "chompi[%d].value", i);
                    json_res = JSON_Search(
                        opt_file, len, query, strlen(query), &value, &value_len);
                    if(json_res == JSONSuccess)
                    {
                        save = value[value_len];
                        value[value_len] = '\0';
                        const int v = atoi(value);
                        if(field == 11 && (v == 0 || v == 4 || v == 8 || v == 12 || v == 16 || v == 24 || v == 32))
                            loop_quantize_grid = static_cast<uint8_t>(v);
                        else if(field == 12 && v >= 0 && v <= 100)
                            loop_quantize_strength = static_cast<uint8_t>(v);
                        value[value_len] = save;
                    }
                }
                else if(field == 1 || field == 2 || field == 3 || field == 5 || field == 9)
                {
                    sprintf(query, "chompi[%d].value", i);
                    json_res = JSON_Search(
                        opt_file, len, query, strlen(query), &value, &value_len);
                    save = value[value_len];
                    value[value_len] = '\0';

                    const uint8_t val = atoi(value) - 1;
                    // midi channels
                    if(val < 16 && json_res == JSONSuccess && (field == 1 || field == 2 || field == 3))
                    {
                        if(field == 1)
                            midi_ch_in = val;
                        else if(field == 2)
                            midi_ch_out_chroma = val;
                        else if(field == 3)
                            midi_ch_out_slice = val;
                    }
                    // monitor position
                    else if(val > 0 && val < 3 && field == 4)
                    {
                        monitor_position = val;
                    }
                    // transport behavior
                    else if(val > 0 && val < 4 && field == 9)
                    {
                        transport_type = val;
                    }

                    value[value_len] = save;
                }
            }
        }
    }

    bool record_latch;
    uint8_t midi_ch_in;
    uint8_t midi_ch_out_chroma;
    uint8_t midi_ch_out_slice;
    bool midi_clock_out;
    uint8_t monitor_position;
    bool midi_cc_in;
    bool midi_cc_out;
    uint8_t transport_type;
    bool delay_mute;
    uint8_t loop_quantize_grid;     // notes per whole note, 0 = off
    uint8_t loop_quantize_strength; // 0-100 %
    
    /**
    * if true, the shift menu is quantized, and normal is not.
    * if false, normal is quantized, and the shift menu is not.
    */
    bool pitch_shift_quantization;

    private:
        FIL fptr_opt;

        static const size_t kOptFileSize = 4096;
        static const size_t kNumOptions = 13;
        char opt_file[kOptFileSize];
};
} // namespace chompi