"""Double-click entry point; importing it does not start the game."""
from tools.launcher import main

if __name__ == "__main__":
    raise SystemExit(main())
