  if (validKHolds == 1)
    goto L2;
  /* "VALID^k holds for this message" = no */
  /* "cascade re-evaluate higher rounds" = no */
  /* "mark (sender, k, v) valid" = no */
  /* "signal round k complete" = no */
  doCascade = 0;
  doMarkValid = 0;
  doSignalComplete = 0;
  if (haveStored == 0)
    goto L5;
  /* "have stored from sender in round k" = yes */
  /* "store (sender, k, v)" = no */
  doStore = 0;
L7:;
  if (postMarkAtNT == 0)
    goto L8;
  /* "post-mark VALID^k count >= n-t" = yes */
L10:;
  if (roundKComplete == 0)
    goto L11;
  /* "round k already complete" = yes */
  goto Ldone;
L11:;
  /* "round k already complete" = no */
  goto Ldone;
L8:;
  /* "post-mark VALID^k count >= n-t" = no */
  goto L10;
L5:;
  /* "have stored from sender in round k" = no */
  /* "store (sender, k, v)" = yes */
  doStore = 1;
  goto L7;
L2:;
  /* "VALID^k holds for this message" = yes */
  if (haveStored == 0)
    goto L14;
  /* "cascade re-evaluate higher rounds" = no */
  /* "have stored from sender in round k" = yes */
  /* "mark (sender, k, v) valid" = no */
  /* "signal round k complete" = no */
  /* "store (sender, k, v)" = no */
  doCascade = 0;
  doMarkValid = 0;
  doSignalComplete = 0;
  doStore = 0;
  goto L7;
L14:;
  /* "have stored from sender in round k" = no */
  /* "mark (sender, k, v) valid" = yes */
  /* "store (sender, k, v)" = yes */
  doMarkValid = 1;
  doStore = 1;
  if (postMarkAtNT == 0)
    goto L17;
  /* "cascade re-evaluate higher rounds" = yes */
  /* "post-mark VALID^k count >= n-t" = yes */
  doCascade = 1;
  if (roundKComplete == 0)
    goto L20;
  /* "round k already complete" = yes */
  /* "signal round k complete" = no */
  doSignalComplete = 0;
  goto Ldone;
L20:;
  /* "round k already complete" = no */
  /* "signal round k complete" = yes */
  doSignalComplete = 1;
  goto Ldone;
L17:;
  /* "cascade re-evaluate higher rounds" = no */
  /* "post-mark VALID^k count >= n-t" = no */
  /* "signal round k complete" = no */
  doCascade = 0;
  doSignalComplete = 0;
  goto L10;
Ldone:;
