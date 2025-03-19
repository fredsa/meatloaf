#include "VFSCommands.h"

#include <cstring>
#include <cerrno>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/syslimits.h>
#include <iostream>

#include "../Console.h"
#include "../Helpers/PWDHelpers.h"
#include "../ute/ute.h"

using namespace ESP32Console;

int cat(int argc, char **argv)
{
    if (argc == 1)
    {
        fprintf(stderr, "You have to pass at least one file path!\r\n");
        return EXIT_SUCCESS;
    }

    for (int n = 1; n < argc; n++)
    {
        std::unique_ptr<MFile> path(getCurrentPath()->cd(argv[n]));
        Meat::iostream istream(path.get());

        if(istream.is_open()) {
            if(istream.eof()) {
                fprintf(stderr, "Stream returned EOF!");
            } else {    
                while(!istream.eof()) {
                    char chr = istream.get();
                    fprintf(stdout, "%c", chr);
                }        
            }
            istream.close();
        }
        else {
            fprintf(stderr, "ERROR:%s could not be read!\r\n", path->url.c_str());
        }
    }

    return EXIT_SUCCESS;
}

int hex(int argc, char **argv)
{
    if (argc == 1)
    {
        fprintf(stderr, "You have to pass at least one file path!\r\n");
        return EXIT_SUCCESS;
    }

    for (int n = 1; n < argc; n++)
    {
        std::unique_ptr<MFile> path(getCurrentPath()->cd(argv[n]));
        Meat::iostream istream(path.get());

        if(istream.is_open()) {
            if(istream.eof()) {
                fprintf(stderr, "Stream returned EOF!");
            } else {
                int c = 0;
                int address = 0;
                char b[17] = {0};
                while(!istream.eof()) 
                {
                    char chr = istream.get();

                    if ( c == 0 )
                    {
                        fprintf(stdout, "%04X: ", address);
                        address += 0x10;
                    }

                    if ( !istream.eof() )
                    {
                        fprintf(stdout, "%02X ", chr);

                        // replace non-printable characters
                        if ( chr < 32 || chr > 126 )
                            chr = '.';

                        b[c] = chr;
                    }

                    // add padding
                    if ( istream.eof() )
                    {
                        if ( c <= 0x07 )
                        {
                            while ( c++ < 0x08 )
                                fprintf(stdout, "   ");

                            fprintf(stdout, "| ");
                            c--;
                        }

                        while ( c++ < 0x10 )
                            fprintf(stdout, "   ");
                    }
                    else if ( c++ == 0x07 )
                    {
                        // add separator
                        fprintf(stdout, "| ");
                    }

                    // show line data as ascii
                    if ( c >= 0x10 )
                    {
                        fprintf(stdout, " |%-16s|\r\n", b);
                        c = 0;
                        memset(b, 0, sizeof(b));
                    }
                }
                fprintf(stdout, "\r\n");
                fprintf(stdout, "url[%s] size[%ld]\r\n", path->url.c_str(), path->size);
            }
            istream.close();
        }
        else {
            fprintf(stderr, "ERROR:%s could not be read!\r\n", path->url.c_str());
        }
    }

    return EXIT_SUCCESS;
}

int pwd(int argc, char **argv)
{
    printf("%s\r\n", getCurrentPath()->url.c_str());
    return EXIT_SUCCESS;
}

int cd(int argc, char **argv)
{
    const char *path;

    if (argc != 2)
    {
        path = getenv("HOME");
        if (!path)
        {
            fprintf(stderr, "No HOME env variable set!\r\n");
            return EXIT_FAILURE;
        }
    }
    else
    {
        path = argv[1];
    }

    std::unique_ptr<MFile> destPath(getCurrentPath()->cd(argv[1]));

    if(destPath->isDirectory()) {        
        auto outgoingPath = getCurrentPath();
        currentPath = outgoingPath->cd(argv[1]);
        delete outgoingPath;
    } else {
        fprintf(stderr, "cd: not a directory: %s\r\n", path);
        return 1;
    }

    return EXIT_SUCCESS;
}

int ls(int argc, char **argv)
{
    MFile* listPath;
    if (argc == 1)
    {
        listPath = MFSOwner::File(getCurrentPath()->url);
    }
    else if (argc == 2)
    {
        listPath = getCurrentPath()->cd(argv[1]);
    }
    else
    {
        printf("You can pass only one filename!\r\n");
        return 1;
    }

    std::unique_ptr<MFile> destPath(listPath);
    std::unique_ptr<MFile> entry(destPath->getNextFileInDir());

    if(entry.get() == nullptr) {
        fprintf(stderr, "Could not open directory: %s\r\n", strerror(errno));
        return 1;
    }

    while(entry.get() != nullptr) {
        printf("%c %8lu  %s\r\n", (entry->isDirectory()) ? 'd':'-', entry->size, entry->name.c_str());
        entry.reset(destPath->getNextFileInDir());
    }

    return EXIT_SUCCESS;
}

int mv(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "Syntax is mv [ORIGIN] [TARGET]\r\n");
        return 1;
    }

    char old_name[PATH_MAX], new_name[PATH_MAX];

    // Resolve arguments to full path
    ESP32Console::console_realpath(argv[1], old_name);
    ESP32Console::console_realpath(argv[2], new_name);

    // Do rename
    if (rename(old_name, new_name))
    {
        printf("Error moving: %s\r\n", strerror(errno));
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int cp(int argc, char **argv)
{
    //TODO: Shows weird error message
    if (argc != 3)
    {
        fprintf(stderr, "Syntax is cp [ORIGIN] [TARGET]\r\n");
        return 1;
    }

    char old_name[PATH_MAX], new_name[PATH_MAX];

    // Resolve arguments to full path
    ESP32Console::console_realpath(argv[1], old_name);
    ESP32Console::console_realpath(argv[2], new_name);

    // Do copy
    FILE *origin = fopen(old_name, "r");
    if (!origin)
    {
        fprintf(stderr, "Error opening origin file: %s\r\n", strerror(errno));
        return 1;
    }

    FILE *target = fopen(new_name, "w");
    if (!target)
    {
        fclose(origin);
        fprintf(stderr, "Error opening target file: %s\r\n", strerror(errno));
        return 1;
    }

    int buffer;

    // Clear existing errors
    auto error = errno;

    while ((buffer = getc(origin)) != EOF)
    {
        if(fputc(buffer, target) == EOF) {
            fprintf(stderr, "Error writing: %s\r\n", strerror(errno));
            fclose(origin); fclose(target);
            return 1;
        }
    }

    error = errno;
    if (error && !feof(origin))
    {
        fprintf(stderr, "Error copying: %s\r\n", strerror(error));
        fclose(origin);
        fclose(target);
        return 1;
    }

    fclose(origin);
    fclose(target);

    return EXIT_SUCCESS;
}

int rm(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "You have to pass exactly one file. Syntax rm [FILE]\r\n");
        return EXIT_SUCCESS;
    }

    char filename[PATH_MAX];
    ESP32Console::console_realpath(argv[1], filename);
    //Debug_printv("filename[%s]", filename);

    if ( strlen(filename) > 1 && filename[strlen(filename) - 1] == '*' )
    {
        char path[PATH_MAX];
        ESP32Console::console_realpath(".", path);

        DIR *dir = opendir(path);
        struct dirent *d;
        while ((d = readdir(dir)) != NULL)
        {
            std::string pattern = filename;
            std::string match_file = path;
            if (strlen(path) > 1)
                match_file += "/";
            match_file += d->d_name;
            //Debug_printv("pattern[%s] match_file[%s]", pattern.c_str(), match_file.c_str());
            if ( mstr::compare(pattern, match_file, false) )
            {
                if (remove(match_file.c_str()))
                {
                    fprintf(stderr, "Error removing %s: %s\r\n", filename, strerror(errno));
                    closedir(dir);
                    return EXIT_FAILURE;
                }
                printf("%s removed\r\n", d->d_name);
            }
        }
        closedir(dir);
    }
    else
    {
        if(remove(filename)) {
            fprintf(stderr, "Error removing %s: %s\r\n", filename, strerror(errno));
            return EXIT_FAILURE;
        }
        printf("%s removed\r\n", filename);
    }

    return EXIT_SUCCESS;
}

int rmdir(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "You have to pass exactly one file. Syntax rmdir [DIRECTORY]\r\n");
        return EXIT_SUCCESS;
    }

    std::unique_ptr<MFile> rd(getCurrentPath()->cd(argv[1]));

    if(rd->rmDir()) {
        fprintf(stderr, "Error deleting %s: %s\r\n", rd->url.c_str(), strerror(errno));
    }

    return EXIT_SUCCESS;
}

int mkdir(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "You have to pass exactly one file. Syntax mkdir [DIRECTORY]\r\n");
        return EXIT_SUCCESS;
    }

    std::unique_ptr<MFile> md(getCurrentPath()->cd(argv[1]));

    if(md->mkDir()) {
        fprintf(stderr, "Error creating %s: %s\r\n", md->url.c_str(), strerror(errno));
    }

    return EXIT_SUCCESS;
}

int mount(int argc, char **argv)
{
    if (argc < 3)
    {
        //fprintf(stderr, "mount {device id} {url/path/filename}\r\n");

        for (int i = 0; i < MAX_DISK_DEVICES; i++)
        {
            // Show device status
            auto drive = Meatloaf.get_disks(i);
            if (drive != nullptr)
            {
                fprintf(stdout, "#%02d: %s\r\n", i + 8, drive->disk_dev.getCWD().c_str()); //"%d: %s\r\n", drive->disk_dev.getCWD().c_str();
            }
        }

        return EXIT_SUCCESS;
    }

    if (!mstr::isNumeric(argv[1]))
    {
        fprintf(stderr, "device id is not numeric\r\n");
        return EXIT_SUCCESS;
    }

    int did = atoi(argv[1]) - 8;

    std::string filename = argv[2];
    if ( !mstr::contains(argv[2], ":") )
    {
        filename = "/" +getCurrentPath()->url + "/" + filename;
    }

    Debug_printv("device id[%d] url[%s]", did, filename.c_str());

    auto drive = Meatloaf.get_disks(did);
    if (drive != nullptr)
    {
        //drive->disk_dev.m_host->mount();
        drive->disk_dev.mount(NULL, filename.c_str(), 0);
    }
    else
    {
        fprintf(stderr, "Error mounting: device #%02d not enabled\r\n", did);
    }

    return EXIT_SUCCESS;
}

int wget(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "wget {url}\r\n");
        return EXIT_SUCCESS;
    }

    std::string pwd = std::string(ESP32Console::console_getpwd());

    auto f = MFSOwner::File(argv[1]);
    if (f != nullptr)
    {
        auto s = f->getSourceStream();

        std::string outfile = pwd;
        outfile += f->name;

        Debug_printv("size[%lu] name[%s] url[%s] outfile[%s]", f->size, f->name.c_str(), s->url.c_str(), outfile.c_str());


        FILE *file = fopen(outfile.c_str(), "w");
        if (file == nullptr)
        {
            fprintf(stdout, "2 Error: Can't open file!\r\n");
            return 2;
        }

        // Receive File
        int count = 0;
        uint8_t bytes[256];
        while (true)
        {
            int bytes_read = s->read(bytes, 256);
            if (bytes_read < 1)
            {
                if (s->available())
                    fprintf(stdout, "\nError reading '%s'\r", f->name.c_str());
                break;
            }

            int bytes_written = fwrite(bytes, 1, bytes_read, file);
            if (bytes_written != bytes_read)
            {
                fprintf(stdout, "\nError writing '%s'\r", f->name.c_str());
                break;
            }

            // Show percentage complete in stdout
            uint8_t percent = (s->position() * 100) / s->size();
#ifdef ENABLE_DISPLAY
            DISPLAY.progress = percent;
#endif
            fprintf(stdout, "Downloading '%s' %d%% [%lu]\r", f->name.c_str(), percent, s->position());
            count++;
        }
        fclose(file);
        fprintf(stdout, "\n");
        delete f;
    }

#ifdef ENABLE_DISPLAY
    DISPLAY.idle();
#endif

    return EXIT_SUCCESS;
}

namespace ESP32Console::Commands
{
    const ConsoleCommand getCatCommand()
    {
        return ConsoleCommand("cat", &cat, "Show the content of one or more files.");
    }

    const ConsoleCommand getHexCommand()
    {
        return ConsoleCommand("hex", &hex, "Show the content of one or more files as hex.");
    }

    const ConsoleCommand getPWDCommand()
    {
        return ConsoleCommand("pwd", &pwd, "Show the current working dir");
    }

    const ConsoleCommand getCDCommand()
    {
        return ConsoleCommand("cd", &cd, "Change the working directory");
    }

    const ConsoleCommand getLsCommand()
    {
        return ConsoleCommand("ls", &ls, "List the contents of the given path");
    }

    const ConsoleCommand getMvCommand()
    {
        return ConsoleCommand("mv", &mv, "Move the given file to another place or name");
    }

    const ConsoleCommand getCPCommand()
    {
        return ConsoleCommand("cp", &cp, "Copy the given file to another place or name");
    }

    const ConsoleCommand getRMCommand()
    {
         return ConsoleCommand("rm", &rm, "Permanenty deletes the given file.");
    }

    const ConsoleCommand getRMDirCommand()
    {
        return ConsoleCommand("rmdir", &rmdir, "Permanenty deletes the given folder. Folder must be empty!");
    }

    const ConsoleCommand getMKDirCommand()
    {
        return ConsoleCommand("mkdir", &mkdir, "Create the DIRECTORY(ies), if they do not already exist.");
    }

    const ConsoleCommand getEditCommand()
    {
        return ConsoleCommand("edit", &ute, "Edit files");
    }

    const ConsoleCommand getMountCommand()
    {
        return ConsoleCommand("mount", &mount, "Mount url on device id");
    }

    const ConsoleCommand getWgetCommand()
    {
        return ConsoleCommand("wget", &wget, "Download url to file");
    }
}