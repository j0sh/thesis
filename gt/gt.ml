(* Requires libav-ocaml from
    https://github.com/j0sh/libav-ocaml
    ( as of 480c3c5f2f ) *)

let _ =
let srcf = Sys.argv.(1) in
let dstf = Sys.argv.(2) in
let src = Libav.to_matrix (Libav.get_image srcf) in
let dst = Libav.to_matrix (Libav.get_image dstf) in
let h = Array.length dst in
let w = Array.length dst.(0) in
let get_r x = x lsr 16 in
let get_g x = (x lsr 8) land 0xFF in
let get_b x = x land 0xFF in
let diff x r g b =
    let a = r - get_r(x) in
    let c = g - get_g(x) in
    let d = b - get_b(x) in
    a*a + c*c + d*d in

let cmp r g b x z =
    let (y, best) = z in
    let score = diff x r g b in
    if score < best then (x, score) else z in

let find_match pixel =
    let r = get_r pixel in
    let g = get_g pixel in
    let b = get_b pixel in

    let best_of_row row =
        let baz = cmp r g b in
        Array.fold_right baz row (-1, 0xFFFFFF) in

    let best_of_col x a =
        let b = best_of_row x in
        let (_, score) = b in
        let (_, best) = a in
        if score < best then b else a in

    let (rgb, _) = Array.fold_right best_of_col src (-1, 0xFFFFFF) in
    rgb in

Printf.printf "Running: src %s dst %s out %s\n" (Sys.argv.(1)) (Sys.argv.(2)) (Sys.argv.(3));
print_newline ();
let cache = Hashtbl.create (w*h) in
let foo x =
    if Hashtbl.mem cache x then
        Hashtbl.find cache x
    else let rgb = find_match x in Hashtbl.add cache x rgb; rgb in
let res = Array.map (Array.map foo) dst in
Libav.write_image (Libav.from_matrix res) Sys.argv.(3)
