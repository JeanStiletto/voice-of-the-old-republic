using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace KeyMouseAccessibilityTest
{
    public class MouseOperations
    {
        //Below are the assembly references that attach to the mouse.
        #region Assembly references
        //Grab Set cursor Position from user32.dll
        [DllImport("user32.dll", EntryPoint = "SetCursorPos")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool SetCursorPos(int x, int y);

        //Grab The Mouse Event Function from user32.dll
        [DllImport("user32.dll", SetLastError = true)]
        static extern void mouse_event(int dwFlags, int dx, int dy, int dwData, int dwExtraInfo);
        private const int MOUSE_EVENT_MOVE = 0x0001;
        private const int MOUSE_EVENT_LEFTDOWN = 0x0002;
        private const int MOUSE_EVENT_LEFTUP = 0x0004;
        private const int MOUSE_EVENT_RIGHTDOWN = 0x0008;
        private const int MOUSE_EVENT_RIGHTUP = 0x0010;
        private const int MOUSE_EVENT_MIDDLEDOWN = 0x0020;
        private const int MOUSE_EVENT_MIDDLEUP = 0x0040;
        private const int MOUSE_EVENT_ABSOLUTE = 0x8000;
        #endregion

        public int mouseSpeed = 10; //Mouse speed in arbitrary speed units
        public double x_dir = 0; //X component of the mouse movemetn vector
        public double y_dir = 0; //Y component of the mouse movement vector
        public dirBitFlags curr_mov; //Current Movement diretcions

        // Left clicks the mouse
        public static void LeftDown()
        {
            mouse_event(MOUSE_EVENT_LEFTDOWN, Cursor.Position.X, Cursor.Position.Y, 0, 0);
        }

        public static void LeftUp()
        {
            mouse_event(MOUSE_EVENT_LEFTUP, Cursor.Position.X, Cursor.Position.Y, 0, 0);
        }

        // Right clicks the mouse
        public static void RightDown()
        {
            mouse_event(MOUSE_EVENT_RIGHTDOWN, Cursor.Position.X, Cursor.Position.Y, 0, 0);
        }

        public static void RightUp()
        {
            mouse_event(MOUSE_EVENT_RIGHTUP, Cursor.Position.X, Cursor.Position.Y, 0, 0);
        }

        public void adjMovAtomic(dirBitFlags f, bool set)
        {
            lock (this)
            {
                if (set) curr_mov |= f;
                else curr_mov &= ~f;

                int x_mag = (int)(
                    ((int)(curr_mov & dirBitFlags.DOWN_RIGHT) >> 2) +
                    ((int)(curr_mov & dirBitFlags.RIGHT) >> 4) +
                    ((int)(curr_mov & dirBitFlags.UP_RIGHT) >> 7) -
                    ((int)(curr_mov & dirBitFlags.DOWN_LEFT) >> 0) -
                    ((int)(curr_mov & dirBitFlags.LEFT) >> 3) -
                    ((int)(curr_mov & dirBitFlags.UP_LEFT) >> 5));

                int y_mag = (int)(
                    ((int)(curr_mov & dirBitFlags.DOWN_RIGHT) >> 2) +
                    ((int)(curr_mov & dirBitFlags.DOWN) >> 1) +
                    ((int)(curr_mov & dirBitFlags.DOWN_LEFT) >> 0) -
                    ((int)(curr_mov & dirBitFlags.UP_RIGHT) >> 7) -
                    ((int)(curr_mov & dirBitFlags.UP) >> 6) -
                    ((int)(curr_mov & dirBitFlags.UP_LEFT) >> 5));

                double mag = Math.Sqrt(x_mag * x_mag + y_mag * y_mag);
                if (mag <= 0)
                {
                    x_dir = 0;
                    y_dir = 0;
                }
                else
                {
                    x_dir = x_mag / mag;
                    y_dir = y_mag / mag;
                }
            }
        }
    }
}
