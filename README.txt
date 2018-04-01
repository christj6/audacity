Audacity fork:

The game plan:
1) remove all the features from Audacity that do not have to do with:
	a) recording audio from mic/stereo mix
	b) importing/exporting audio files
	c) trimming audio
2) Have the "Open" button do what the Import Audio button does now
3) Have the "Save" button do what the Export Audio buttons do now
4) Remove the concept of a "Project"
5) Remove the whole concept of "tracks" (one file at a time, that's it)

I intend to only use/test this fork with a Windows machine. I won't touch any of the Mac/Linux specific stuff unless I really have to. If push comes to shove, I'll yank that stuff out too.

COMPILING:
Check audacity\win\compile.txt

The short version is: install Visual Studio 2013, install Python, install wxWidgets-3.0.2, set your environment paths correctly, etc...

