import pysu2, math
from mpi4py import MPI

def main():
  comm = MPI.COMM_WORLD

  # Initialize the corresponding driver of SU2, this includes solver preprocessing.
  try:
    SU2Driver = pysu2.CSinglezoneDriver('turb_SA_flatplate.cfg', 1, comm)
  except TypeError as exception:
    print('A TypeError occured in pysu2.CDriver : ', exception)
    raise

  # Get the ID of the marker we want to deform.
  AllMarkerIDs = SU2Driver.GetMarkerIndices()
  MarkerName = 'wall'
  MarkerID = AllMarkerIDs[MarkerName] if MarkerName in AllMarkerIDs else -1

  # Number of vertices on the specified marker (per rank).
  nVertex = SU2Driver.GetNumberMarkerNodes(MarkerID) if MarkerID >= 0 else 0

  # Apply a load based on the coordinates.
  if nVertex > 0:
    MarkerCoords = SU2Driver.MarkerCoordinates(MarkerID)
    L = 0.5
    dx = L / 24 
    for iVertex in range(nVertex):
      x = MarkerCoords(iVertex, 0)
      nodalForce = (20 * (L - x) / L) * dx
      SU2Driver.SetMarkerCustomFEALoad(MarkerID, iVertex, ( nodalForce, 0))

  
  SU2Driver.StartSolver()
  SU2Driver.Finalize()


if __name__ == '__main__':

  main()
