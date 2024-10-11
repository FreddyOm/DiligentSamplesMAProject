//
// This example program reads a .binvox file and writes
// an ASCII version of the same file called "voxels.txt"
//
// 0 = empty voxel
// 1 = filled voxel
// A newline is output after every "dim" voxels (depth = height = width = dim)
//
// Note that this ASCII version is not supported by "viewvox" and "thinvox"
//
// The x-axis is the most significant axis, then the z-axis, then the y-axis.
//
#include "binvox_loader.h"
#include <fstream>
#include <iostream>
#include <stdlib.h>

using namespace std;

//int   version;
//int   depth, height, width;
//int   size;
//byte* voxels = 0;
//float tx, ty, tz;
//float scale;

size_t get_index(int x, int y, int z, BinvoxData data)
{
    size_t index = x * (data.width * data.height) + z * data.width + y; // wxh = width * height = d * d
    return index;

}

BinvoxData read_binvox(string filespec)
{
    BinvoxData data{};

    ifstream* input = new ifstream(filespec.c_str(), ios::in | ios::binary);

    //
    // read header
    //
    string line;
    *input >> line; // #binvox


    VERIFY_EXPR(line.compare("#binvox") == 0);

    *input >> data.version;

    data.depth = -1;
    int done   = 0;
    while (input->good() && !done)
    {
        *input >> line;
        if (line.compare("data") == 0) done = 1;
        else if (line.compare("dim") == 0)
        {
            *input >> data.depth >> data.height >> data.width;
        }
        else if (line.compare("translate") == 0)
        {
            *input >> data.tx >> data.ty >> data.tz;
        }
        else if (line.compare("scale") == 0)
        {
            *input >> data.scale;
        }
        else
        {
            cout << "  unrecognized keyword [" << line << "], skipping" << endl;
            char c;
            do { // skip until end of line
                c = (char)input->get();
            } while (input->good() && (c != '\n'));
        }
    }

    VERIFY_EXPR(done);
    VERIFY_EXPR(data.depth != -1);


    data.size   = data.width * data.height * data.depth;
    data.voxels = new byte[data.size];

    VERIFY_EXPR(data.voxels != nullptr);

    //
    // read voxel data
    //
    byte value;
    byte count;
    int  index     = 0;
    int  end_index = 0;
    int  nr_voxels = 0;

    input->unsetf(ios::skipws); // need to read every byte now (!)
    *input >> value;            // read the linefeed char

    while ((end_index < data.size) && input->good())
    {
        *input >> value >> count;

        if (input->good())
        {
            end_index = index + count;
            if (end_index > data.size) return data;
            for (int i = index; i < end_index; i++) data.voxels[i] = value;

            if (value) nr_voxels += count;
            index = end_index;
        } // if file still ok

    } // while

    input->close();
    cout << "  read " << nr_voxels << " voxels" << endl;

    delete input;

    return data;
}

/*
int _main(int argc, char** argv)
{
    if (argc != 2)
    {
        cout << "Usage: read_binvox <binvox filename>" << endl
             << endl;
        exit(1);
    }

    if (!read_binvox(argv[1]))
    {
        cout << "Error reading [" << argv[1] << "]" << endl
             << endl;
        exit(1);
    }

    //
    // now write the data to as ASCII
    //
    ofstream* out = new ofstream("voxels.txt");
    if (!out->good())
    {
        cout << "Error opening [voxels.txt]" << endl
             << endl;
        exit(1);
    }

    cout << "Writing voxel data to ASCII file..." << endl;

    *out << "#binvox ASCII data" << endl;
    *out << "dim " << data.depth << " " << data.height << " " << data.width << endl;
    *out << "translate " << data.tx << " " << data.ty << " " << data.tz << endl;
    *out << "scale " << data.scale << endl;
    *out << "data" << endl;

    for (int i = 0; i < data.size; i++)
    {
        *out << (char)(data.voxels[i] + '0') << " ";
        if (((i + 1) % data.width) == 0) *out << endl;
    }

    out->close();

    cout << "done" << endl
         << endl;

    return 0;
}

*/