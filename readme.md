# This repository has been moved to [gitlab.com/paul-nechifor/pnstat](http://gitlab.com/paul-nechifor/pnstat).

Old readme:

# PN Stat

A short daemon for monitoring and logging certain Linux values. I created this
program to run as a daemon to monitor my EEE-PC Arch-Linux torrent seedbox. It's
supposed to be very, very lightweight. It's purpose is to collect the raw values
and write them to the log files. No processing whatsoever.

![PN Stat cover image.](screenshot.png)

It's so lightweight that the only way to configure it for your system is to edit
the macros in the C source file. (2013 note: this made be laugh.)

## The log file structure

When the program starts a new header is added which looks like this:

    =mem_free;mem_total;bat_last_full;fan_speed

It starts adding values separated by `;`:

    1800000;2000000;1000;1500

When a new line is added, if the last value is identical, the value will be
replaced with a null string:

    1800100;;1001;    - means 1800100;2000000;1001;1500

Except that the last `;` are eliminated. So if just the `bat_last_full` changes
the line will be:

    ;;1001

This is why the values should be arranged by their likelihood of changing. The
most variable being first.

If the log grows beyond 1 MiB (the default value) the log file will be renamed
`log000001` and an new `log` file will be created. When that file grows beyond
the limit, `log000002` will be created. The next number is determined by
incrementing the largest number log file. This is done so that the log files
will always be in order and so that you can remove older log files.

Creating a new log doesn't mean that the headers will be added. The purpose of
the headers is to indicate to the program that does the processing what the
values mean. So they are only written at startup because the values may change
because if the program is modified and recompiled.

## How to process the logs

To generate the full log you concatenate the log files in order and then add the
`log` file (so `log000002`, `log000003`, and `log`).

You read line by line. If the line starts with `=` it is a header line. The
header names are separated by `;`. The following lines contain the values for
those names in order again separated by `;`. If the value is empty (i.e. "")
then it hasn't changed. If the line contains fewer values than the number of
columns, then those values haven't changed.

If a new header is found, then the columns might change order, disappear or
might be new ones.

Python example of processing:

```python
# This dictionary will contain the list of values for each of the column names.
valuesDict = {}
headers = None

def aNewLine(line):
    if line.startswith("="):
        headers = line[1:].split(";")
    else:
        values = line.split(";")
        for i in xrange(len(values)):
            try:
                vect = valuesDict[headers[i]]
                vect.append(values[i])
            except KeyError:
                vect = [values[i]]
                valuesDict[headers[i]] = vect
            if vect[-1] == "":
                vect[-1] = vect[-2]
```
