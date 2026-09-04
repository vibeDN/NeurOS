# NeurOS interactive-shell greeting -> fastfetch with the current agent's logo
function fish_greeting
    if type -q neurofetch
        neurofetch
    end
end
