Spinup Example
==============
The first simulation will be of a transient "spinup" phase.

If the simulation completes, the last time step reported will be 14400 at time 7200 s, and the job directory will contain
checkpoint (chk\*) and plotfile (plt\*) directories, along with a post_processing/ directory. Once the spinup simulation is done,
it is helpful to sanity check that the flow field variables make sense. The ``plt#####`` files can be opened using Paraview or other
visualization software that is AMReX-compatible. Another quick test is to plot the evolution of horizontally averaged vertical profiles,
which are provided through the ``abl_statistics`` file within post_processing/.

Here is the content of our spinup input file:

.. literalinclude:: ./spinup_inp.txt
   :linenos: