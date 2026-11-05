{
  "patcher": {
    "fileversion": 1,
    "appversion": {
      "major": 8,
      "minor": 5,
      "revision": 0,
      "architecture": "x64",
      "modernui": 1
    },
    "classnamespace": "box",
    "rect": [
      100.0,
      100.0,
      700.0,
      500.0
    ],
    "openinpresentation": 1,
    "default_fontsize": 10.0,
    "gridsize": [
      15.0,
      15.0
    ],
    "boxes": [
      {
        "box": {
          "id": "midiin",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 1,
          "patching_rect": [
            30,
            30,
            60,
            22
          ],
          "text": "midiin",
          "outlettype": [
            "int"
          ]
        }
      },
      {
        "box": {
          "id": "midiout",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 0,
          "patching_rect": [
            30,
            70,
            65,
            22
          ],
          "text": "midiout"
        }
      },
      {
        "box": {
          "id": "device",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 3,
          "patching_rect": [
            200,
            30,
            128,
            22
          ],
          "text": "live.this.device",
          "outlettype": [
            "bang",
            "int",
            "int"
          ]
        }
      },
      {
        "box": {
          "id": "init",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 2,
          "patching_rect": [
            200,
            60,
            60,
            22
          ],
          "text": "t b b",
          "outlettype": [
            "bang",
            "bang"
          ]
        }
      },
      {
        "box": {
          "id": "pathmsg",
          "maxclass": "message",
          "numinlets": 2,
          "numoutlets": 1,
          "patching_rect": [
            260,
            90,
            107,
            22
          ],
          "text": "path live_set",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "path",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 3,
          "patching_rect": [
            260,
            120,
            79,
            22
          ],
          "text": "live.path",
          "outlettype": [
            "",
            "",
            ""
          ]
        }
      },
      {
        "box": {
          "id": "rootobs",
          "maxclass": "newobj",
          "numinlets": 2,
          "numoutlets": 2,
          "patching_rect": [
            200,
            160,
            177,
            22
          ],
          "text": "live.observer root_note",
          "outlettype": [
            "",
            ""
          ]
        }
      },
      {
        "box": {
          "id": "root",
          "maxclass": "newobj",
          "numinlets": 2,
          "numoutlets": 1,
          "patching_rect": [
            200,
            190,
            60,
            22
          ],
          "text": "i",
          "outlettype": [
            "int"
          ]
        }
      },
      {
        "box": {
          "id": "rootcc",
          "maxclass": "newobj",
          "numinlets": 3,
          "numoutlets": 0,
          "patching_rect": [
            200,
            220,
            107,
            22
          ],
          "text": "ctlout 102 16"
        }
      },
      {
        "box": {
          "id": "scaleobs",
          "maxclass": "newobj",
          "numinlets": 2,
          "numoutlets": 2,
          "patching_rect": [
            420,
            160,
            219,
            22
          ],
          "text": "live.observer scale_intervals",
          "outlettype": [
            "",
            ""
          ]
        }
      },
      {
        "box": {
          "id": "bits",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 1,
          "patching_rect": [
            420,
            190,
            114,
            22
          ],
          "text": "vexpr 1 << $i1",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "sum",
          "maxclass": "newobj",
          "numinlets": 2,
          "numoutlets": 2,
          "patching_rect": [
            420,
            220,
            60,
            22
          ],
          "text": "zl.sum",
          "outlettype": [
            "",
            ""
          ]
        }
      },
      {
        "box": {
          "id": "mask",
          "maxclass": "newobj",
          "numinlets": 2,
          "numoutlets": 1,
          "patching_rect": [
            420,
            250,
            60,
            22
          ],
          "text": "i",
          "outlettype": [
            "int"
          ]
        }
      },
      {
        "box": {
          "id": "order",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 2,
          "patching_rect": [
            420,
            280,
            60,
            22
          ],
          "text": "t i i",
          "outlettype": [
            "int",
            "int"
          ]
        }
      },
      {
        "box": {
          "id": "low",
          "maxclass": "newobj",
          "numinlets": 2,
          "numoutlets": 1,
          "patching_rect": [
            500,
            310,
            60,
            22
          ],
          "text": "& 127",
          "outlettype": [
            "int"
          ]
        }
      },
      {
        "box": {
          "id": "lowcc",
          "maxclass": "newobj",
          "numinlets": 3,
          "numoutlets": 0,
          "patching_rect": [
            500,
            340,
            107,
            22
          ],
          "text": "ctlout 103 16"
        }
      },
      {
        "box": {
          "id": "high",
          "maxclass": "newobj",
          "numinlets": 2,
          "numoutlets": 1,
          "patching_rect": [
            420,
            370,
            60,
            22
          ],
          "text": ">> 7",
          "outlettype": [
            "int"
          ]
        }
      },
      {
        "box": {
          "id": "highcc",
          "maxclass": "newobj",
          "numinlets": 3,
          "numoutlets": 0,
          "patching_rect": [
            420,
            400,
            107,
            22
          ],
          "text": "ctlout 104 16"
        }
      },
      {
        "box": {
          "id": "metro",
          "maxclass": "newobj",
          "numinlets": 2,
          "numoutlets": 1,
          "patching_rect": [
            120,
            130,
            86,
            22
          ],
          "text": "metro 2000",
          "outlettype": [
            "bang"
          ]
        }
      },
      {
        "box": {
          "id": "title",
          "maxclass": "comment",
          "numinlets": 1,
          "numoutlets": 0,
          "patching_rect": [
            10,
            10,
            180,
            22
          ],
          "text": "BASH! Key Lock",
          "presentation": 1,
          "presentation_rect": [
            8,
            8,
            170,
            22
          ],
          "fontsize": 14.0,
          "fontface": 1
        }
      },
      {
        "box": {
          "id": "help",
          "maxclass": "comment",
          "numinlets": 1,
          "numoutlets": 0,
          "patching_rect": [
            10,
            450,
            380,
            22
          ],
          "text": "Put before BASH! on this track. BASH! follows the set's key and scale.",
          "presentation": 1,
          "presentation_rect": [
            8,
            34,
            170,
            70
          ],
          "linecount": 4
        }
      }
    ],
    "lines": [
      {
        "patchline": {
          "source": [
            "midiin",
            0
          ],
          "destination": [
            "midiout",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "device",
            0
          ],
          "destination": [
            "init",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "init",
            1
          ],
          "destination": [
            "pathmsg",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "pathmsg",
            0
          ],
          "destination": [
            "path",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "path",
            0
          ],
          "destination": [
            "rootobs",
            1
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "rootobs",
            0
          ],
          "destination": [
            "root",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "root",
            0
          ],
          "destination": [
            "rootcc",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "path",
            0
          ],
          "destination": [
            "scaleobs",
            1
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "scaleobs",
            0
          ],
          "destination": [
            "bits",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "bits",
            0
          ],
          "destination": [
            "sum",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "sum",
            0
          ],
          "destination": [
            "mask",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "mask",
            0
          ],
          "destination": [
            "order",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "order",
            1
          ],
          "destination": [
            "low",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "low",
            0
          ],
          "destination": [
            "lowcc",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "order",
            0
          ],
          "destination": [
            "high",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "high",
            0
          ],
          "destination": [
            "highcc",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "init",
            0
          ],
          "destination": [
            "metro",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "metro",
            0
          ],
          "destination": [
            "root",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "metro",
            0
          ],
          "destination": [
            "mask",
            0
          ]
        }
      }
    ],
    "description": "Sends Live's key and scale to BASH!",
    "digest": "",
    "tags": "BASH",
    "devicewidth": 190.0
  }
}